---
name: esp32-c6-waveshare
description: Discover, monitor, and develop firmware for Max's USB-connected Waveshare ESP32-C6-Touch-AMOLED-2.16 in the esp32-playground repository. Use for this board's serial connection, display bring-up, diagnostics, backup, and flashing workflows.
---

# Waveshare ESP32-C6 local workflow

The repository is currently `/Users/max/Developer/github/mfellner/esp32-playground`. This skill's versioned source lives under its `skills/` directory; the personal installation is a symlink. If relocated, find the repository before using its helper.

Read `AGENTS.md`, `docs/hardware.md`, and `docs/interaction.md` in that repository for hardware operations. Read `notes/2026-09-05-discovery.md` for factory firmware evidence, and `docs/references.md` when sourcing board drivers or a toolchain.

Enumerate with `uv run scripts/esp32_serial.py list` from the repository. Select the known USB identity `303a:1001`, serial `D4:05:92:B9:04:28`, not a hardcoded port; `/dev/cu.usbmodem2101` was observed on macOS. The USB descriptor is shared with other Espressif devices, so use serial identity and boot logs to distinguish boards.

Use `uv run scripts/esp32_serial.py monitor --seconds 5` for a bounded receive-only session. Opening may reset the board even with DTR/RTS false; the first capture reported `USB_UART_HPSYS`. Close monitors before esptool. The factory console has no verified shell or remote display protocol.

The factory image identifies ESP32-C6 rev v0.2, 16 MB flash, `01_Fac`, and ESP-IDF v5.5.3. Vendor controller names CO5300/CST9220 differ from software labels sh8601/CST9217; inspect the exact board demo and schematic before choosing initialization sequences. Do not reuse ESP32-S3 or other display-size pin maps.

For requested firmware replacement, preserve a full flash backup and checksum first when readable, follow the selected project's generated flashing arguments, and record the result in notes. Existing user authorization governs execution; documentation/discovery alone is not a firmware replacement request. Avoid eFuse or security changes as incidental setup. Raw backups/logs remain ignored. The current application is `firmware/sparkdash`, using ESP-IDF 5.5.3 installed outside Git at `/Users/max/esp/esp-idf-v5.5.3`. Activate its `export.sh` before building. Read `docs/sparkdash-validation.md` and the bring-up notes for actual gates and backup metadata. Normal USB diagnostics support STATUS/NEXT/PREV; validation-only server controls require a separate test build and must not ship in releases.
