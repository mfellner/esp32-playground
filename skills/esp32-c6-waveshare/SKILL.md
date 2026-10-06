---
name: esp32-c6-waveshare
description: Discover, monitor, flash and switch apps on Max's USB-connected Waveshare ESP32-C6-Touch-AMOLED-2.16 running the esp32-playground multi-app platform (launcher + Sparklet + reserved slot). Use for this board's serial connection, backups, app installs, boot selection, buttons and recovery.
---

# Waveshare ESP32-C6 local workflow

The platform repository is `mfellner/esp32-playground`, checked out locally at `/Users/max/Developer/github/mfellner/esp32-playground`. This skill's versioned source is in its `skills/` directory, and the personal installation is a symlink to it. If the repository has moved, find it before running its helpers. App repositories, such as `mfellner/sparklet` (local `../sparklet`), build the images that go into the slots.

Before any hardware work, read `AGENTS.md`, `docs/hardware.md`, `docs/interaction.md` and `docs/platform.md` in the platform repository. `notes/` holds the dated evidence; the latest is `notes/2026-10-06-multi-app-platform.md`.

## Finding the board

- Enumerate with `uv run scripts/esp32_serial.py list`.
- Select the known USB identity `303a:1001` with serial `D4:05:92:B9:04:28`, not a hardcoded port. `/dev/cu.usbmodem2101` is the port that has been observed.
- Opening the port resets the board. Captures must stay bounded:
  ```sh
  uv run scripts/esp32_serial.py monitor --seconds 8 --delay 4 --send STATUS
  ```
- Close monitors before using esptool or `tools/device.py`.

## Flash layout and installs

The device uses the platform layout:

| Slot | Offset | Size | Contents |
| --- | --- | --- | --- |
| launcher (factory) | 0x20000 | 2 MiB | launcher |
| sparklet (ota_0) | 0x220000 | 4 MiB | Sparklet |
| hermes (ota_1) | 0x620000 | 4 MiB | reserved |

NVS is shared: the `nvs` partition sits at 0x9000 (64 KiB).

- **Inspect** with `uv run tools/device.py status`.
- **Install** with `tools/device.py install <slot> <build-dir> [--boot]`, or run `idf.py -p PORT <slot>-flash` in the app project.
- **Switch** with `tools/device.py boot <slot>`, or use `recover` to start the launcher.
- **Never run `idf.py flash` from an app project.** The platform guard blocks it, because it would overwrite the launcher.
- Only `idf.py -C firmware/launcher -p PORT flash` writes the bootloader and partition table.

## Buttons

| Button | In an app | At reset |
| --- | --- | --- |
| KEY (GPIO10) | Short press opens the launcher | Holding it opens the launcher once |
| BOOT (GPIO9) | Hold for at least 1 s to open the launcher | Holding it enters ROM download mode |
| PWR (AXP2101) | Short press is app-defined | Holding it for 6 s powers off |

## Rules

- **Firmware changes.** Before any requested firmware change, make a full backup with `tools/device.py backup` and use generated flash arguments. Record the results in `notes/`.
- **Authorization.** Existing user authorization governs execution. Discovery and documentation work alone is not a firmware-change request.
- **Security settings.** Don't change eFuses or security configuration.
- **Ignored data.** Raw backups and logs stay ignored.
- **SDK.** ESP-IDF 5.5.3 is installed outside Git at `/Users/max/esp/esp-idf-v5.5.3`; activate it with `export.sh`.
- **USB commands.** The launcher answers `STATUS`; Sparklet accepts `STATUS`, `NEXT` and `PREV`. QA builds add `TEST_*` controls, which are never shipped.
