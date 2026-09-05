# Flash recovery

This procedure was physically exercised on the known Waveshare ESP32-C6-Touch-AMOLED-2.16 on 2026-09-05. A full factory image was restored and booted as `01_Fac`; the saved full sparkDash snapshot was then restored, booted, and reconnected to all five nodes without reconfiguration. Both writes passed esptool's data verification. See the bring-up notes for evidence and hashes.

## Before connecting

Close serial monitors and run `uv run scripts/esp32_serial.py list` from the repository root. Select VID/PID `303a:1001`, USB serial `D4:05:92:B9:04:28`. The examples below use the port observed during validation; re-enumerate it for each session. Opening USB can reboot the display.

Ordinary application updates use `idf.py -p PORT flash` from `firmware/sparkdash` and preserve NVS. Full-image restoration writes the entire device, including its configuration. Preserve a fresh full image before testing a factory restore if you want to return to the current configuration.

## Preserve the current state

Use pinned esptool 5.4.0. Probe flash size first; the verified board has 16 MB:

```sh
uvx --from esptool==5.4.0 esptool --chip esp32c6 --port /dev/cu.usbmodem2101 flash-id
```

Choose a **new** backup filename. The existence guard below prevents overwriting a previous file:

```sh
task_backup_path=backups/current-before-restore.bin
test ! -e "$task_backup_path" && uvx --from esptool==5.4.0 esptool --chip esp32c6 --port /dev/cu.usbmodem2101 read-flash 0 ALL "$task_backup_path"
```

Stop on any error or unreadable/protected flash; do not use force/protection overrides. A complete image for this board is exactly 16,777,216 bytes. Calculate SHA-256 and store adjacent metadata with byte count, device identity, capture time, and tool version. Keep backups and raw logs ignored. Do not proceed to replacement with only an incomplete backup.

## Verify the selected original backup

The preserved original is `backups/factory-2026-09-05-1040.bin`. Its adjacent JSON contains the reviewed checksum. This host-only check was used during validation:

```sh
python3 - <<'PY'
import hashlib, json
from pathlib import Path
backup = Path('backups/factory-2026-09-05-1040.bin')
metadata = json.loads(backup.with_suffix('.json').read_text())
assert metadata['usb_serial'] == 'D4:05:92:B9:04:28'
assert backup.stat().st_size == metadata['bytes'] == 16777216
assert hashlib.sha256(backup.read_bytes()).hexdigest() == metadata['sha256']
print('Backup identity, length and SHA-256 verified')
PY
```

Stop if any assertion fails. The original checksum is also recorded in `notes/2026-09-05-firmware-bringup.md`.

## Restore and check

With monitors closed, restore the verified full image at address zero. `keep` retains the image's flash header settings:

```sh
uvx --from esptool==5.4.0 esptool --chip esp32c6 --port /dev/cu.usbmodem2101 write-flash --flash-mode keep --flash-size keep --flash-freq keep 0 backups/factory-2026-09-05-1040.bin
```

Require the successful data-verification result. Capture a bounded boot log using a new ignored filename:

```sh
uv run scripts/esp32_serial.py monitor --seconds 8 --output logs/factory-restored-new.bin
```

The tested factory boot identified `01_Fac`, version `1`, initialized its LCD and 480 × 480 touch controller, and returned from `app_main`. Do not send sparkDash diagnostic commands to the factory application.

To return to a saved custom snapshot, repeat the size/checksum verification with that snapshot and its metadata, then use the same full-image write command with its filename. The snapshot from this validation is `backups/sparkdash-pre-restore-2026-09-05.bin`. Its checksum and metadata are kept next to it. After restoring it, `uv run tools/check_device.py --seconds 60 --output logs/restored-live-new.json` verifies reconnection and live polling.

A full image is always restored at zero. An application release uses its **generated flash arguments**, which describe multiple binaries and their partition offsets; follow the release bundle's `FLASH.txt`. No eFuses or security settings are modified by this workflow.
