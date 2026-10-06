# Working in this repository

This repository targets the **Waveshare ESP32-C6-Touch-AMOLED-2.16**, not an ESP32-S3 or a different display size. Read `docs/hardware.md`, `docs/interaction.md` and `docs/platform.md` before interacting with hardware. Use the repository skill at `skills/esp32-c6-waveshare/SKILL.md` for USB/firmware tasks, even if it is not in the current skill catalog. App repositories such as `mfellner/sparklet` defer to these rules for device work.

- Discover the current port with `uv run scripts/esp32_serial.py list`. Known USB identity: VID:PID `303a:1001`, serial `D4:05:92:B9:04:28`. Do not select an arbitrary board when several are attached.
- Opening the serial port can reboot this board, even with DTR/RTS initially false. Keep monitoring bounded, and close the port before esptool or another monitor uses it. After a reset the port may briefly disappear; retry enumeration instead of guessing another port.
- Logs are not a shell. Only firmware-defined diagnostic lines exist (`STATUS`; QA builds add `TEST_*` controls). Test-only USB controls are compiled out of normal releases.
- The device runs the multi-app platform layout (`components/app_switch/layout/partitions.csv`). Never run `idf.py flash`/`app-flash` from an app project. Install with `tools/device.py install <slot> <build>` or `idf.py -p PORT <slot>-flash`. Only the launcher project may write the bootloader, partition table and otadata.
- Never write NVS partitions as a side effect. Apps share the default `nvs` partition through their own namespaces and must not erase the whole partition. The launcher's "Reset settings" is the only all-apps erase.
- Preserve the installed firmware during discovery/documentation tasks. For requested firmware changes, first make a full flash backup if readable (`tools/device.py backup`), checksum it, and keep it outside Git. Use the project's generated flash arguments, not guessed offsets. Honor existing user authorization without redundant approval requests.
- No eFuse burning, security configuration changes, or forced flash protection overrides as incidental setup steps.
- Distinguish observed device facts from vendor specifications and untested procedures. Resolve initialization and pin details against the exact board's schematic/source before implementing drivers. Factory driver labels (sh8601/CST9217) differ from the vendor's controller names (CO5300/CST9220).
- Keep credentials and raw flash/log data in ignored local files. Commit only reviewed, relevant diagnostic excerpts.
- Update notes after meaningful hardware discoveries. Validate host helpers with enumeration and bounded reads; verify firmware with its build and real device behaviour.

ESP-IDF projects here (`firmware/launcher`, `tests/firmware/crash_app`) and every app on the device are pinned to SDK v5.5.3 at `/Users/max/esp/esp-idf-v5.5.3`. Activate its `export.sh` before builds, and verify the SDK is available in a new environment. The launcher project's bootloader starts all apps, so upgrade the SDK for all images together. Python and uv are available; each script declares its own dependencies.
