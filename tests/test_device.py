"""Host tests for tools/device.py (no device access). Run: python3 -m unittest discover tests"""

import hashlib
import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("device", ROOT / "tools/device.py")
device = importlib.util.module_from_spec(spec)
sys.modules["device"] = device
spec.loader.exec_module(device)

TYPES = {"app": 0, "data": 1}
SUBTYPES = {"nvs": 2, "ota": 0, "phy": 1, "factory": 0, "littlefs": 0x83}


def table_from_csv(path: Path) -> bytes:
    entries = b""
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        label, ptype, subtype, offset, size = [f.strip() for f in line.split(",")[:5]]
        sub = (
            int(subtype[4:]) + 0x10 if subtype.startswith("ota_") else SUBTYPES[subtype]
        )
        entries += struct.pack(
            "<HBBII16sI",
            0x50AA,
            TYPES[ptype],
            sub,
            int(offset, 0),
            int(size, 0),
            label.encode(),
            0,
        )
    md5 = b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries).digest()
    data = entries + md5
    return data + b"\xff" * (device.TABLE_SIZE - len(data))


class TableTests(unittest.TestCase):
    def setUp(self):
        self.data = table_from_csv(device.LAYOUT / "partitions.csv")
        self.parts = device.parse_table(self.data)

    def test_layout(self):
        labels = [p.label for p in self.parts]
        self.assertEqual(
            labels,
            [
                "nvs",
                "otadata",
                "phy_init",
                "launcher",
                "sparklet",
                "hermes",
                "nvs_hermes",
                "storage",
            ],
        )
        self.assertEqual(device.find(self.parts, "sparklet").offset, 0x220000)
        self.assertEqual(device.find(self.parts, "hermes").ota_index, 1)
        self.assertIsNone(device.find(self.parts, "launcher").ota_index)
        self.assertEqual(device.ota_count(self.parts), 2)
        self.assertEqual(self.parts[-1].end, device.FLASH_SIZE)
        for a, b in zip(self.parts, self.parts[1:]):
            self.assertLessEqual(a.end, b.offset)
        for p in self.parts:
            if p.is_app:
                self.assertEqual(p.offset % 0x10000, 0)

    def test_nvs_offset_matches_pre_platform_layout(self):
        # The migration keeps Sparklet's saved settings only if NVS stays at 0x9000/64 KiB.
        nvs = device.find(self.parts, "nvs")
        self.assertEqual((nvs.offset, nvs.size), (0x9000, 0x10000))

    def test_md5_mismatch(self):
        broken = bytearray(self.data)
        broken[20] ^= 1
        with self.assertRaises(device.DeviceError):
            device.parse_table(bytes(broken))

    def test_table_bytes_ignores_padding(self):
        self.assertEqual(
            device.table_bytes(self.data), device.table_bytes(self.data + b"\xff" * 64)
        )

    def test_check_writes(self):
        device.check_writes([(0x220000, 0x1B0000)], self.parts)
        device.check_writes(
            [(0x0, 0x5820), (0x8000, 0xC00)], self.parts, allow_boot_area=True
        )
        for offset, size in [
            (0x9000, 0x1000),
            (0xA20000, 0x10),
            (0x21F000, 0x2000),
            (0x0, 0x10),
        ]:
            with self.assertRaises(device.DeviceError):
                device.check_writes([(offset, size)], self.parts)
        with self.assertRaises(device.DeviceError):  # larger than the slot
            device.check_writes([(0x220000, 0x400001)], self.parts)


class OtadataTests(unittest.TestCase):
    blank = b"\xff" * 0x2000

    def apply(self, data, sector, image):
        data = bytearray(data)
        data[sector * device.SECTOR : (sector + 1) * device.SECTOR] = image
        return bytes(data)

    def test_blank_boots_factory(self):
        self.assertIsNone(device.boot_ota_index(self.blank, 2))

    def test_select_sequence_matches_idf(self):
        data = self.blank
        history = []
        for target in [0, 1, 1, 0, 1, 0, 0]:
            sector, image = device.select_ota(data, target, 2)
            data = self.apply(data, sector, image)
            self.assertEqual(device.boot_ota_index(data, 2), target)
            history.append((sector, struct.unpack_from("<I", image)[0]))
        # esp_rewrite_ota_data: first write seq=index+1 to sector 0, then alternate sectors.
        self.assertEqual(history[0], (0, 1))
        self.assertEqual(history[1], (1, 2))
        self.assertEqual([s for s, _ in history], [0, 1, 0, 1, 0, 1, 0])

    def test_crc_matches_idf_written_entry(self):
        # Read back from the device on 2026-10-06 after esp_ota_set_boot_partition(sparklet).
        self.assertEqual(device.otadata_crc(1), 0x4743989A)

    def test_entry_format(self):
        _sector, image = device.select_ota(self.blank, 0, 2)
        seq, _, state, crc = struct.unpack_from("<I20sII", image)
        self.assertEqual((seq, state), (1, 0xFFFFFFFF))
        self.assertEqual(crc, device.otadata_crc(1))

    def test_invalid_crc_ignored(self):
        sector, image = device.select_ota(self.blank, 1, 2)
        broken = bytearray(image)
        broken[28] ^= 0xFF
        self.assertIsNone(
            device.boot_ota_index(self.apply(self.blank, sector, broken), 2)
        )

    def test_out_of_range(self):
        with self.assertRaises(device.DeviceError):
            device.select_ota(self.blank, 2, 2)


class ImageTests(unittest.TestCase):
    def test_app_description(self):
        header = bytearray(b"\xff" * 0x100)
        header[0] = 0xE9
        desc = struct.pack(
            "<IIII32s32s16s16s32s",
            device.APP_DESC_MAGIC,
            0,
            0,
            0,
            b"1.1.0",
            b"sparkdash",
            b"00:00:00",
            b"Jan 1 2026",
            b"v5.5.3",
        )
        header[0x20 : 0x20 + len(desc)] = desc
        self.assertEqual(
            device.app_description(bytes(header)),
            {"version": "1.1.0", "project_name": "sparkdash", "idf": "v5.5.3"},
        )
        self.assertIsNone(device.app_description(b"\xff" * 0x100))

    def test_flash_args(self):
        with tempfile.TemporaryDirectory() as d:
            args = Path(d) / "sparklet-flash_args"
            args.write_text(
                "--flash_mode dio --flash_freq 80m --flash_size 16MB\n0x220000 sparkdash.bin\n"
            )
            self.assertEqual(
                device.parse_flash_args(args), [(0x220000, Path(d) / "sparkdash.bin")]
            )
            args.write_text(
                "--flash_mode dio --flash_freq 80m --flash_size 4MB\n0x10000 a.bin\n"
            )
            with self.assertRaises(device.DeviceError):
                device.parse_flash_args(args)

    def test_slots_registry_matches_layout(self):
        parts = device.parse_table(table_from_csv(device.LAYOUT / "partitions.csv"))
        slots = device.load_slots()
        for app in slots["apps"]:
            self.assertIsNotNone(device.find(parts, app["label"]).ota_index)
        self.assertEqual(device.find(parts, slots["launcher"]["label"]).subtype, 0)


if __name__ == "__main__":
    unittest.main()
