# Working in this repository

This repository targets the **Waveshare ESP32-C6-Touch-AMOLED-2.16**, not an ESP32-S3 or a different display size. Read `docs/hardware.md` and `docs/interaction.md` before interacting with hardware. Use the repository skill at `skills/esp32-c6-waveshare/SKILL.md` for USB/firmware tasks even if it is not in the current skill catalog.

- Discover the current port with `uv run scripts/esp32_serial.py list`. Known USB identity: VID:PID `303a:1001`, serial `D4:05:92:B9:04:28`. Do not select an arbitrary board when several are attached.
- Serial opening can reboot this board even with DTR/RTS initially false. Keep monitoring bounded and close the port before esptool or another monitor uses it.
- Logs are not a shell. Screen, touch, and audio control require an implemented firmware protocol; none has been verified in the factory app.
- Preserve the installed firmware during discovery/documentation tasks. For requested replacement firmware, first make a full flash backup if readable, checksum it, and keep it outside Git. Use the actual project's generated flash arguments, not guessed offsets. Honor existing user authorization without redundant approval requests.
- No eFuse burning, security configuration changes, or forced flash protection overrides as incidental setup steps.
- Distinguish observed device facts from vendor specifications and untested procedures. In particular, factory driver labels differ from the vendor's controller names; resolve initialization and pin details against the exact board's source/schematic before implementing drivers.
- Keep credentials and raw flash/log data in ignored local files. Commit only reviewed, relevant diagnostic excerpts.
- Update notes after meaningful hardware discoveries. Validate host helpers with enumeration and bounded reads as appropriate; verify firmware using its build and real device behavior when firmware exists.

The ESP-IDF project is `firmware/sparkdash`, pinned to SDK v5.5.3 at `/Users/max/esp/esp-idf-v5.5.3`. Activate its `export.sh` before builds; verify SDK availability rather than assuming it in a new environment. Python and uv are available; helper dependencies are declared in each script. Read `docs/sparkdash-validation.md` for completed and outstanding gates. The user excluded the 24-hour soak from v1 completion. Test-only USB controls are compiled out of normal releases.
