#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["esptool==5.4.0", "pyserial==3.5"]
# ///
"""Install, switch and inspect apps on the multi-app Waveshare ESP32-C6-Touch-AMOLED-2.16.

Every write uses offsets from generated build files and is checked against the partition table
that is actually on the device. NVS partitions are never written.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LAYOUT = ROOT / "components/app_switch/layout"
BOARD_SERIAL = "D4:05:92:B9:04:28"
BOARD_USB = (0x303A, 0x1001)
FLASH_SIZE = 16 * 1024 * 1024
TABLE_OFFSET = 0x8000
TABLE_SIZE = 0xC00
APP_DESC_OFFSET = 0x20  # image header (0x18) + first segment header (8)
APP_DESC_MAGIC = 0xABCD5432
OTA_MIN, OTA_MAX = 0x10, 0x20
SECTOR = 0x1000


class DeviceError(Exception):
    pass


@dataclass(frozen=True)
class Partition:
    label: str
    type: int
    subtype: int
    offset: int
    size: int

    @property
    def end(self) -> int:
        return self.offset + self.size

    @property
    def is_app(self) -> bool:
        return self.type == 0

    @property
    def ota_index(self) -> int | None:
        return (
            self.subtype - OTA_MIN
            if self.is_app and OTA_MIN <= self.subtype < OTA_MAX
            else None
        )


def parse_table(data: bytes) -> list[Partition]:
    """Parses an ESP-IDF binary partition table (32-byte entries, magic 0xAA50, MD5 entry 0xEBEB)."""
    partitions = []
    md5_seen = False
    for i in range(0, len(data) - 31, 32):
        entry = data[i : i + 32]
        if entry[:2] == b"\xff\xff":
            break
        if entry[:2] == b"\xeb\xeb":
            if hashlib.md5(data[:i]).digest() != entry[16:32]:
                raise DeviceError("partition table MD5 mismatch")
            md5_seen = True
            continue
        magic, ptype, subtype, offset, size = struct.unpack_from("<HBBII", entry)
        if magic != 0x50AA:
            raise DeviceError(
                f"invalid partition table entry at 0x{TABLE_OFFSET + i:x}"
            )
        label = entry[12:28].split(b"\0", 1)[0].decode("ascii")
        partitions.append(Partition(label, ptype, subtype, offset, size))
    if not partitions:
        raise DeviceError("no partition table found")
    if not md5_seen:
        raise DeviceError("partition table has no MD5 entry")
    return partitions


def find(partitions: list[Partition], label: str) -> Partition:
    for p in partitions:
        if p.label == label:
            return p
    raise DeviceError(f"partition '{label}' is not in the table")


def table_bytes(data: bytes) -> bytes:
    """Significant bytes of a table image: entries up to and including the MD5 entry."""
    for i in range(0, len(data) - 31, 32):
        if data[i : i + 2] == b"\xff\xff":
            return data[:i]
    return data


# --- otadata -------------------------------------------------------------------------------------


def otadata_crc(seq: int) -> int:
    # esp_rom_crc32_le(UINT32_MAX, &seq, 4) == zlib.crc32(data, 0xFFFFFFFF)
    return zlib.crc32(struct.pack("<I", seq), 0xFFFFFFFF)


def parse_otadata(data: bytes) -> list[int | None]:
    """Returns the valid sequence number of each otadata sector, or None."""
    result = []
    for sector in (0, 1):
        seq, _label, state, crc = struct.unpack_from("<I20sII", data, sector * SECTOR)
        valid = seq != 0xFFFFFFFF and crc == otadata_crc(seq) and state not in (3, 4)
        result.append(seq if valid else None)
    return result


def boot_ota_index(data: bytes, ota_count: int) -> int | None:
    """OTA index the bootloader selects, or None for factory (blank/invalid otadata)."""
    seqs = [s for s in parse_otadata(data) if s is not None]
    if not seqs or not ota_count:
        return None
    return (max(seqs) - 1) % ota_count


def select_ota(data: bytes, index: int, ota_count: int) -> tuple[int, bytes]:
    """Mirrors esp_rewrite_ota_data(): returns (sector index, new 4 KiB sector image)."""
    if not 0 <= index < ota_count:
        raise DeviceError("OTA index outside the partition table")
    seqs = parse_otadata(data)
    if any(s is not None for s in seqs):
        # bootloader_common_select_otadata(): on equal sequence numbers sector 0 is active.
        if None not in seqs:
            active = 0 if seqs[0] >= seqs[1] else 1
        else:
            active = 0 if seqs[0] is not None else 1
        seq = seqs[active]
        n = 0
        while seq > (index + 1) % ota_count + n * ota_count:
            n += 1
        new_seq, sector = (index + 1) % ota_count + n * ota_count, (~active) & 1
    else:
        new_seq, sector = index + 1, 0
    entry = struct.pack(
        "<I20sII", new_seq, b"\xff" * 20, 0xFFFFFFFF, otadata_crc(new_seq)
    )
    return sector, entry + b"\xff" * (SECTOR - len(entry))


# --- app images ----------------------------------------------------------------------------------


def app_description(header: bytes) -> dict | None:
    if len(header) < APP_DESC_OFFSET + 0xB0 or header[0] != 0xE9:
        return None
    desc = header[APP_DESC_OFFSET:]
    if struct.unpack_from("<I", desc)[0] != APP_DESC_MAGIC:
        return None

    def text(start: int, size: int) -> str:
        return desc[start : start + size].split(b"\0", 1)[0].decode("utf-8", "replace")

    return {
        "version": text(0x10, 32),
        "project_name": text(0x30, 32),
        "idf": text(0x70, 32),
    }


# --- build inputs --------------------------------------------------------------------------------


def load_slots() -> dict:
    return json.loads((LAYOUT / "slots.json").read_text())


def parse_flash_args(path: Path) -> list[tuple[int, Path]]:
    lines = path.read_text().split("\n")
    if not lines or "--flash_size 16MB" not in lines[0]:
        raise DeviceError(f"{path}: unexpected flash settings")
    images = []
    for line in lines[1:]:
        for offset, name in zip(line.split()[0::2], line.split()[1::2]):
            images.append((int(offset, 0), path.parent / name))
    if not images:
        raise DeviceError(f"{path}: no images")
    return images


@dataclass
class AppBuild:
    label: str
    project_name: str
    images: list[tuple[int, Path]]
    table: bytes


def app_build(label: str, build_dir: Path, slots: dict) -> AppBuild:
    expected = {a["label"]: a for a in slots["apps"]}
    if label not in expected:
        raise DeviceError(f"'{label}' is not an app slot in slots.json")
    description = json.loads((build_dir / "project_description.json").read_text())
    want = expected[label].get("project_name")
    if want and description["project_name"] != want:
        raise DeviceError(
            f"{build_dir} builds '{description['project_name']}', slot expects '{want}'"
        )
    args = build_dir / f"{label}-flash_args"
    if not args.is_file():
        raise DeviceError(
            f"{args} missing; the project must call platform_app_slot({label})"
        )
    table = (build_dir / "partition_table/partition-table.bin").read_bytes()
    images = parse_flash_args(args)
    if len(images) != 1:
        raise DeviceError(f"{args}: expected exactly one app image")
    return AppBuild(label, description["project_name"], images, table)


def launcher_build(build_dir: Path) -> tuple[list[tuple[int, Path]], bytes]:
    description = json.loads((build_dir / "project_description.json").read_text())
    if description["project_name"] != load_slots()["launcher"]["project_name"]:
        raise DeviceError(f"{build_dir} is not a launcher build")
    flasher = json.loads((build_dir / "flasher_args.json").read_text())
    if flasher["flash_settings"]["flash_size"] != "16MB":
        raise DeviceError("launcher build has unexpected flash size")
    images = [(int(o, 0), build_dir / f) for o, f in flasher["flash_files"].items()]
    table = (build_dir / "partition_table/partition-table.bin").read_bytes()
    return images, table


def check_writes(
    images: list[tuple[int, int]], partitions: list[Partition], allow_boot_area=False
):
    """Every write must sit inside one non-NVS partition (or the bootloader/table area)."""
    for offset, size in images:
        end = offset + size
        if allow_boot_area and end <= TABLE_OFFSET + SECTOR:
            continue
        inside = [p for p in partitions if p.offset <= offset and end <= p.end]
        if len(inside) != 1:
            raise DeviceError(
                f"write 0x{offset:x}+0x{size:x} is not inside exactly one partition"
            )
        if inside[0].type == 1 and inside[0].subtype == 0x02:
            raise DeviceError(f"refusing to write NVS partition '{inside[0].label}'")


# --- device access -------------------------------------------------------------------------------


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    from serial.tools import list_ports

    matches = [
        p.device
        for p in list_ports.comports()
        if (p.vid, p.pid) == BOARD_USB and p.serial_number == BOARD_SERIAL
    ]
    if len(matches) != 1:
        raise DeviceError(
            "expected exactly one known board; close monitors or pass --port"
        )
    return matches[0]


class Device:
    """One esptool session (opening it resets the board into the ROM loader)."""

    def __init__(self, port: str):
        from esptool import cmds

        self.cmds = cmds
        self.esp = cmds.detect_chip(port, baud=115200)
        if self.esp.CHIP_NAME != "ESP32-C6":
            raise DeviceError(f"unexpected chip {self.esp.CHIP_NAME}")
        self.esp = self.esp.run_stub()
        self.esp.change_baud(921600)
        cmds.attach_flash(self.esp)
        size = cmds.flash_size_bytes(cmds.detect_flash_size(self.esp) or "")
        if size != FLASH_SIZE:
            raise DeviceError(f"unexpected flash size {size}")

    def read(self, offset: int, size: int) -> bytes:
        return self.cmds.read_flash(self.esp, offset, size, no_progress=True)

    def write(self, images: list[tuple[int, bytes]]):
        self.cmds.write_flash(
            self.esp, images, flash_freq="keep", flash_mode="keep", flash_size="keep"
        )
        for offset, data in images:
            self.cmds.verify_flash(self.esp, [(offset, data)])

    def table(self) -> tuple[bytes, list[Partition]]:
        data = self.read(TABLE_OFFSET, TABLE_SIZE)
        return data, parse_table(data)

    def close(self, reset=True):
        if reset:
            self.cmds.reset_chip(self.esp, "hard-reset")
        self.esp._port.close()


def open_device(args) -> Device:
    return Device(find_port(args.port))


def otadata_partition(partitions: list[Partition]) -> Partition:
    for p in partitions:
        if p.type == 1 and p.subtype == 0x00:
            return p
    raise DeviceError("partition table has no otadata")


def ota_count(partitions: list[Partition]) -> int:
    return sum(1 for p in partitions if p.ota_index is not None)


def select_boot(dev: Device, partitions: list[Partition], label: str):
    ota = otadata_partition(partitions)
    if label == "launcher":
        dev.write([(ota.offset, b"\xff" * ota.size)])
        return
    target = find(partitions, label)
    if target.ota_index is None:
        raise DeviceError(f"'{label}' is not an OTA app slot")
    desc = app_description(dev.read(target.offset, 0x100))
    if not desc:
        raise DeviceError(f"no app image in '{label}'; install one first")
    expected = {a["label"]: a.get("project_name") for a in load_slots()["apps"]}.get(
        label
    )
    if expected and desc["project_name"] != expected:
        raise DeviceError(
            f"'{label}' holds '{desc['project_name']}', expected '{expected}'"
        )
    sector, image = select_ota(
        dev.read(ota.offset, ota.size), target.ota_index, ota_count(partitions)
    )
    dev.write([(ota.offset + sector * SECTOR, image)])


# --- commands ------------------------------------------------------------------------------------


def cmd_status(args):
    dev = open_device(args)
    try:
        _, partitions = dev.table()
        ota = next((p for p in partitions if p.type == 1 and p.subtype == 0x00), None)
        boot = (
            boot_ota_index(dev.read(ota.offset, ota.size), ota_count(partitions))
            if ota
            else None
        )
        if not ota:
            print("No otadata: single-app layout (not migrated to the platform layout)")
        print(f"{'label':<11}{'type':<6}{'offset':>10}{'size':>10}  contents")
        for p in partitions:
            contents = ""
            if p.is_app:
                desc = app_description(dev.read(p.offset, 0x100))
                contents = (
                    f"{desc['project_name']} {desc['version']}" if desc else "(empty)"
                )
                selected = p.ota_index == boot if boot is not None else p.subtype == 0
                contents += "  <- boots next" if selected else ""
            kind = "app" if p.is_app else "data"
            print(f"{p.label:<11}{kind:<6}{p.offset:>#10x}{p.size:>#10x}  {contents}")
    finally:
        dev.close()


def write_backup_metadata(path: Path, data: bytes, note: str):
    meta = {
        "file": path.name,
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "usb_serial": BOARD_SERIAL,
        "chip": "ESP32-C6",
        "flash_bytes": FLASH_SIZE,
        "esptool": "5.4.0",
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "content": note,
    }
    path.with_suffix(".json").write_text(json.dumps(meta, indent=2) + "\n")
    return meta


def cmd_backup(args):
    path = (
        args.output
        or ROOT / "backups" / f"device-{datetime.now(timezone.utc):%Y-%m-%d-%H%M}.bin"
    )
    if path.exists() or path.with_suffix(".json").exists():
        raise DeviceError(f"{path} exists; refusing overwrite")
    dev = open_device(args)
    try:
        data = dev.read(0, FLASH_SIZE)
    finally:
        dev.close()
    if len(data) != FLASH_SIZE:
        raise DeviceError("incomplete read")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as f:
        f.write(data)
    meta = write_backup_metadata(path, data, args.note)
    print(f"{path} {meta['sha256']}")


def verified_backup(path: Path, max_age_hours: float) -> bytes:
    meta = json.loads(path.with_suffix(".json").read_text())
    data = path.read_bytes()
    if (
        meta.get("usb_serial") != BOARD_SERIAL
        or len(data) != FLASH_SIZE
        or meta.get("bytes") != FLASH_SIZE
        or hashlib.sha256(data).hexdigest() != meta.get("sha256")
    ):
        raise DeviceError(
            f"{path}: identity, size or checksum does not match its metadata"
        )
    age = datetime.now(timezone.utc) - datetime.fromisoformat(meta["captured_utc"])
    if age.total_seconds() > max_age_hours * 3600:
        raise DeviceError(
            f"{path} is older than {max_age_hours} h; take a fresh backup"
        )
    return data


def cmd_migrate(args):
    backup = verified_backup(args.backup, args.max_backup_age)
    launcher_images, table = launcher_build(args.launcher_build)
    partitions = parse_table(
        table
    )  # the launcher build enforced the layout CSV at configure time
    apps = [app_build(label, Path(build), load_slots()) for label, build in args.app]
    for app in apps:
        if table_bytes(app.table) != table_bytes(table):
            raise DeviceError(
                f"{app.label} build uses a different partition table than the launcher"
            )
    images = [(o, p.read_bytes()) for o, p in launcher_images]
    for app in apps:
        slot = find(partitions, app.label)
        ((offset, path),) = app.images
        if offset != slot.offset:
            raise DeviceError(
                f"{app.label}: generated offset 0x{offset:x} != table 0x{slot.offset:x}"
            )
        images.append((offset, path.read_bytes()))
    check_writes([(o, len(d)) for o, d in images], partitions, allow_boot_area=True)
    nvs = find(partitions, "nvs")
    dev = open_device(args)
    try:
        current, _ = dev.table()
        if table_bytes(current) == table_bytes(table):
            raise DeviceError(
                "device already uses the platform layout; use install/boot instead"
            )
        if (
            dev.read(TABLE_OFFSET, TABLE_SIZE)
            != backup[TABLE_OFFSET : TABLE_OFFSET + TABLE_SIZE]
        ):
            raise DeviceError("backup does not match the device's partition table")
        before = hashlib.sha256(dev.read(nvs.offset, nvs.size)).hexdigest()
        print("Writing:", ", ".join(f"0x{o:x} ({len(d)} B)" for o, d in images))
        dev.write(images)
        if args.boot:
            select_boot(dev, partitions, args.boot)
        after = hashlib.sha256(dev.read(nvs.offset, nvs.size)).hexdigest()
        if before != after:
            raise DeviceError("NVS changed during migration (unexpected)")
        print(
            f"Migrated; NVS unchanged ({after[:16]}…); next boot: {args.boot or 'launcher'}"
        )
    finally:
        dev.close()


def launcher_app(build_dir: Path) -> AppBuild:
    """The launcher's own app image (factory slot) from its generated flasher_args.json."""
    launcher_build(build_dir)  # validates project and flash size
    flasher = json.loads((build_dir / "flasher_args.json").read_text())
    app = flasher["app"]
    table = (build_dir / "partition_table/partition-table.bin").read_bytes()
    return AppBuild(
        "launcher",
        "launcher",
        [(int(app["offset"], 0), build_dir / app["file"])],
        table,
    )


def cmd_install(args):
    if args.label == "launcher":
        if args.boot:
            raise DeviceError("use 'recover' to boot the launcher")
        app = launcher_app(args.build)
    else:
        app = app_build(args.label, args.build, load_slots())
    ((offset, path),) = app.images
    data = path.read_bytes()
    dev = open_device(args)
    try:
        current, partitions = dev.table()
        if table_bytes(current) != table_bytes(app.table):
            raise DeviceError(
                "device partition table differs from the build's; migrate first"
            )
        slot = find(partitions, args.label)
        if offset != slot.offset or len(data) > slot.size:
            raise DeviceError(f"{path.name} does not fit slot '{args.label}'")
        check_writes([(offset, len(data))], partitions)
        dev.write([(offset, data)])
        if args.boot:
            select_boot(dev, partitions, args.label)
        print(
            f"Installed {app.project_name} into '{args.label}' at 0x{offset:x}"
            + (f"; next boot: {args.label}" if args.boot else "")
        )
    finally:
        dev.close()


def cmd_erase(args):
    dev = open_device(args)
    try:
        _, partitions = dev.table()
        slot = find(partitions, args.label)
        if slot.ota_index is None:
            raise DeviceError(
                "only OTA app slots can be erased (not the launcher or data)"
            )
        ota = otadata_partition(partitions)
        if (
            boot_ota_index(dev.read(ota.offset, ota.size), ota_count(partitions))
            == slot.ota_index
        ):
            raise DeviceError(
                f"'{args.label}' is selected for the next boot; boot another app first"
            )
        dev.cmds.erase_region(dev.esp, slot.offset, slot.size)
        if app_description(dev.read(slot.offset, 0x100)):
            raise DeviceError("slot still contains an image after erase")
        print(f"Erased '{args.label}' (0x{slot.offset:x}+0x{slot.size:x})")
    finally:
        dev.close()


def cmd_boot(args):
    dev = open_device(args)
    try:
        _, partitions = dev.table()
        select_boot(dev, partitions, args.label)
        print(f"Next boot: {args.label}")
    finally:
        dev.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port", help="explicit port; otherwise the known USB serial is selected"
    )
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status", help="show partitions, installed apps and the next boot")
    backup = sub.add_parser("backup", help="full 16 MiB read with checksum metadata")
    backup.add_argument("--output", type=Path)
    backup.add_argument("--note", default="")
    migrate = sub.add_parser(
        "migrate", help="one-time move to the platform layout (keeps NVS)"
    )
    migrate.add_argument(
        "--backup", type=Path, required=True, help="fresh full backup (.bin + .json)"
    )
    migrate.add_argument("--max-backup-age", type=float, default=12.0, help="hours")
    migrate.add_argument("--launcher-build", type=Path, required=True)
    migrate.add_argument(
        "--app", nargs=2, action="append", default=[], metavar=("LABEL", "BUILD")
    )
    migrate.add_argument(
        "--boot", help="app label to boot after migration (default: launcher)"
    )
    install = sub.add_parser(
        "install", help="write one app build (or the launcher) into its slot"
    )
    install.add_argument("label")
    install.add_argument("build", type=Path)
    install.add_argument("--boot", action="store_true", help="also boot it next")
    boot = sub.add_parser(
        "boot", help="select the app (or 'launcher') for the next boot"
    )
    boot.add_argument("label")
    sub.add_parser("recover", help="erase the boot selection so the launcher starts")
    erase = sub.add_parser(
        "erase", help="erase an OTA app slot that is not selected for boot"
    )
    erase.add_argument("label")
    args = parser.parse_args(argv)
    if args.command == "recover":
        args.command, args.label = "boot", "launcher"
    handlers = {
        "status": cmd_status,
        "backup": cmd_backup,
        "migrate": cmd_migrate,
        "install": cmd_install,
        "boot": cmd_boot,
        "erase": cmd_erase,
    }
    handlers[args.command](args)
    return 0


if __name__ == "__main__":
    try:
        from esptool.util import FatalError
    except ImportError:  # pragma: no cover
        FatalError = DeviceError
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except (DeviceError, FatalError, OSError, ValueError, KeyError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
