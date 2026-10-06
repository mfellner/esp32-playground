# ESP32-C6 AMOLED playground

Board knowledge, tooling and a multi-app platform for the **Waveshare ESP32-C6-Touch-AMOLED-2.16** (ESP32-C6, 16 MB flash, 480 × 480 AMOLED, CST9217-class touch, AXP2101 PMIC, QMI8658 IMU).

Several independent ESP-IDF apps share one device. A small launcher in the factory slot lists the installed apps and starts one. Every app can return to the launcher with a touch control or a hardware button. Each app is a separate image, so it gets the chip's whole RAM.

| Slot | Partition | App | Repository |
| --- | --- | --- | --- |
| factory | `launcher` 0x20000, 2 MiB | Launcher (this repo, `firmware/launcher`) | here |
| ota_0 | `sparklet` 0x220000, 4 MiB | [Sparklet](https://github.com/mfellner/sparklet) metrics dashboard | `mfellner/sparklet` |
| ota_1 | `hermes` 0x620000, 4 MiB | reserved for a future port of hermes-gadget-sdk | — |

Shared data partitions:
- `nvs`: 0x9000, 64 KiB, shared through per-app namespaces.
- `nvs_hermes`: an isolated NVS partition for the reserved app.
- `storage`: about 5.8 MiB of LittleFS, reserved.

The canonical table is [`components/app_switch/layout/partitions.csv`](components/app_switch/layout/partitions.csv). The registry is [`slots.json`](components/app_switch/layout/slots.json).

## Using the device

| Gesture | In an app | In the launcher |
| --- | --- | --- |
| Tap a tile | — | Start that app; it stays selected across power cycles |
| App's own **Apps** control (Sparklet: Settings → Apps) | Open the launcher | — |
| **KEY** short press (GPIO10) | Open the launcher | Start the highlighted (last) app |
| **BOOT** hold ≥ 1 s (GPIO9) | Open the launcher | — |
| **PWR** short press | App-defined (Sparklet: dim/wake) | Screen off/on |
| **KEY** held while the board resets or powers on | Launcher opens once; the selected app is unchanged | — |
| **PWR** held 6 s | Power off (PMIC) | Power off (PMIC) |

Leaving an app through the launcher clears the boot selection, so a power cycle then shows the launcher. Starting an app from the launcher selects it again.

If an app is started three times in a row without reporting a healthy start, the bootloader opens the launcher. The launcher explains why on screen.

## Repository map

| Path | Contents |
| --- | --- |
| [`AGENTS.md`](AGENTS.md) | Rules for working with the physical board |
| [`docs/platform.md`](docs/platform.md) | Platform design, boot flow, and how to add an app |
| [`docs/hardware.md`](docs/hardware.md), [`docs/interaction.md`](docs/interaction.md), [`docs/recovery.md`](docs/recovery.md), [`docs/references.md`](docs/references.md) | Board facts, safe USB use, backups and recovery, sources |
| `notes/` | Dated evidence from discovery, bring-up and the platform migration |
| [`components/board`](components/board) | Board support: panel, touch, LVGL adapter, IMU orientation, PMIC power key |
| [`components/app_switch`](components/app_switch) | Launcher client, shared layout, sdkconfig contract and `platform_app_slot()` CMake helper |
| [`firmware/launcher`](firmware/launcher) | Launcher app and the platform bootloader (`bootloader_components/main`) |
| [`tools/device.py`](tools/device.py) | Backup, status, migration, install, boot selection and slot erase |
| `tools/check_switch.py` | Bounded USB switch round trips (QA builds) |
| `scripts/esp32_serial.py` | Port discovery and bounded monitor with optional diagnostic lines |
| `skills/esp32-c6-waveshare` | Agent skill for this board |
| `tests/` | Host tests (C++ with sanitizers, Python unittest) and a crash fixture app |

## Build and install

ESP-IDF **5.5.3** is pinned. Every image on the device has to be built with the same SDK, because the launcher project's bootloader starts all of them.

```sh
. ~/esp/esp-idf-v5.5.3/export.sh
idf.py -C firmware/launcher build
uv run scripts/esp32_serial.py list          # known board: 303a:1001, serial D4:05:92:B9:04:28
```

**A device still on Sparklet's old single-app layout** needs a one-time migration. The migration keeps NVS, so Sparklet's saved Wi-Fi and server settings survive it. Take a fresh full backup first:

```sh
uv run tools/device.py backup --note "before platform migration"
uv run tools/device.py migrate --backup backups/device-<timestamp>.bin \
    --launcher-build firmware/launcher/build \
    --app sparklet ../sparklet/firmware/sparkdash/build --boot sparklet
```

**After the migration**, install or update each image separately. Offsets come from the generated build files, and the tool checks them against the table on the device:

```sh
uv run tools/device.py status
uv run tools/device.py install sparklet ../sparklet/firmware/sparkdash/build --boot
uv run tools/device.py install launcher firmware/launcher/build
uv run tools/device.py boot sparklet         # or: recover (= launcher next)
```

An app project's `idf.py flash` is disabled on purpose, because it would overwrite the launcher. Use `idf.py -p PORT <slot>-flash` or `device.py install`.

**Updating the bootloader:** run `idf.py -C firmware/launcher -p PORT flash`. It writes the bootloader, the partition table, blank otadata and the launcher, so the next boot opens the launcher.

## Tests

```sh
cmake -S tests/host -B tests/host/build && cmake --build tests/host/build && ctest --test-dir tests/host/build
python3 -m unittest discover -s tests
uvx ruff check tools scripts tests
```

`tests/firmware/crash_app` is a validation fixture that aborts on purpose. Install it only temporarily into the reserved slot, to exercise the crash guard.

## Status

The platform was migrated and validated on the device on 2026-10-06:
- NVS was preserved.
- 20 of 20 USB switch round trips passed.
- The buttons, touch controls, crash guard, KEY-at-boot gesture and power cycles all behaved as designed.

See [the evidence](notes/2026-10-06-multi-app-platform.md), including what was not exercised.

Board support derives from Waveshare's example code for this board, and that example repository has no license grant. See [`components/board/PROVENANCE.md`](components/board/PROVENANCE.md).
