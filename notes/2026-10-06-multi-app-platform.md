# Multi-app platform migration — 2026-10-06

The board moved from a single Sparklet image to the platform layout, which has a launcher, a Sparklet slot and a reserved slot. This note records what was observed on the device. Raw logs and backups stay in ignored `logs/` and `backups/` directories.

## Before any write

- **Fresh full backup.** `backups/pre-platform-2026-10-06.bin` was read with esptool 5.4.0.
  - Size: 16,777,216 bytes.
  - SHA-256: `cbef735a0e4010d7243b8d6f241f8a359d22832f0facb1b683d838903635cace`.
  - Contents: Sparklet with automatic rotation, project version string still 1.0.0, in a 6 MiB `factory` partition at 0x20000. NVS sat at 0x9000/64 KiB and `phy_init` at 0x19000.
- **Backup checked against the device region by region.**
  - Bootloader/table, application, unused 0x19000–0x20000 area and the 0x620000–end tail all matched.
  - Only NVS differed, because the device ran between the read and the check.
- **New layout kept NVS in place.** It stays at 0x9000/64 KiB, so the migration did not need to touch it.

## Migration

- **Command.** `tools/device.py migrate` wrote these images, each verified by esptool:
  - the launcher project's bootloader
  - the partition table
  - blank otadata at 0x19000
  - the launcher at 0x20000
  - Sparklet 1.1.0 (QA build) at 0x220000, using `sparklet-flash_args`
  - an otadata entry selecting `sparklet`
- **NVS was untouched.** The tool hashed NVS before and after the writes, and both hashes were `58265d5dab92402b…`.
- **Boot log.** It showed the new eight-entry table and `Loaded app from partition at offset 0x220000`.
- **Saved settings survived.** Sparklet rejoined Wi-Fi with its saved settings and no reconfiguration. A 60-second QA run passed:
  - 5 nodes, 0 errors.
  - Minimum sampled heap 61,728 B; largest block 44,032 B.
  - Spare stack: diagnostics 2,256 B, network 2,896 B, UI 13,740 B.

## Finding: leftover vendor image

The first launcher STATUS reported the reserved `hermes` slot as `valid:xiaozhi:2.4.0`.
- **Cause.** The factory firmware's old `ota_1` partition was also at 0x620000. Sparklet's earlier writes never reached that region.
- **Risk.** Launching it would have booted vendor firmware built for a different layout.
- **Action taken.**
  - Erased the slot with `tools/device.py erase hermes`.
  - The launcher now refuses images whose project does not match the slot registry.
  - `device.py boot` refuses them as well.
- **Not erased.** The `storage` partition, at the old factory `assets`/`storage` offsets, still holds factory data. Nothing mounts it yet, so erase it before first use.

## Switching

**USB round trips (QA launcher `TEST_BOOT`, QA Sparklet `TEST_OPEN_LAUNCHER`).**
- 20 of 20 round trips passed.
- Every launcher start reported `reason=requested slot=0` through the RTC handoff.
- Launcher → Sparklet took 0.95–1.04 s to Sparklet's first log line, including image hash verification by `esp_ota_set_boot_partition`.
- Sparklet → launcher took 0.53–0.62 s.

**otadata compatibility, checked in both directions.**
- **IDF-written entry, read by the tool.** After `esp_ota_set_boot_partition(sparklet)`, sector 0 held sequence 1 with CRC `0x4743989a`. `device.py`'s CRC function reproduces that value, which is now a regression test.
- **Tool-written entry, read by the bootloader.** After `tools/device.py boot sparklet`, the bootloader loaded 0x220000.

**Isolation.**
- After `device.py install sparklet`, `esptool verify-flash` confirmed the launcher, bootloader and partition table were byte-identical.
- `idf.py flash` in the Sparklet project stops with the platform guard message.

## Buttons

**Schematic, checked by reading the exact board's schematic.**
- Key1/BOOT is GPIO9 with R8 10K pull-up.
- Key3/KEY is GPIO10 with R18 10K pull-up.
- Key2/PWR goes to AXP2101 PWRON through RP7. PWRON also drives a BSS138 whose drain is GPIO18 (R11 10K pull-up), so GPIO18 is high while PWR is pressed.
- The schematic pin table lists GPIO10 as `LCD_RESET`. The J4 connector actually routes LCD_RESET through R16 to ALDO3, so that table entry is stale.

**Observed on the device.** In a 180-second session, with the user following written steps:
- **KEY.** STATUS sampled `gpio10=0` and `app_switch: button KEY`.
- **KEY in the launcher** started Sparklet.
- **KEY in Sparklet** returned to the launcher with `reason=requested`.
- **Touch.** Tile taps and the Settings **Apps** button switched apps.
- **PWR.** Two short presses were counted through AXP2101 INTSTS2 (0x49), turning the screen off and back on.
- **Screen.** The user reported the launcher layout readable and the touch targets working.

**BOOT hold (1 s) in Sparklet.**
- The first attempt in that session produced no event; the cause was not isolated.
- A later logged attempt produced `app_switch: button BOOT hold` and `rst:0xc (SW_CPU),boot:0x7f (SPI_FAST_FLASH_BOOT)`, then the launcher.
- So holding BOOT across `esp_restart()` did **not** enter download mode.
- The user also reported the launcher's on-screen BOOT-hold counter increasing. That session's USB log did not capture it, probably because the press happened after the logging window.

**KEY held at reset.**
- The board was reset by opening USB while the user held KEY.
- The platform bootloader logged `KEY held: starting launcher instead of OTA app 0`, and the launcher reported `reason=key`. otadata stayed on Sparklet.

**Power cycle (user report).**
- Unplug/replug while Sparklet runs: Sparklet resumes.
- After leaving through the launcher (KEY), a power cycle shows the launcher.

## Crash guard

**Test.** A fixture app (`tests/firmware/crash_app`, project `crashtest`) aborts before `app_switch_mark_healthy()`. It was installed temporarily into the `hermes` slot.

**Result.**
- `TEST_BOOT hermes` led to three aborts, then the bootloader logged `OTA app 1 failed to start 3 times: starting launcher`.
- The launcher reported `reason=crash slot=1`.
- The launcher then clears the boot selection, and `device.py status` showed the launcher selected.
- The fixture was erased afterwards.

## Final images (normal, non-QA)

**Launcher 0.1.0.**

| Image | Size | SHA-256 |
| --- | --- | --- |
| `launcher.bin` | 738,640 B | `626341b3bae7bf0bf1153969cf3f763500d7bda635b6dd5717ffd356cc494d16` |
| Bootloader | 23,024 B | `87d1a5e3b3ebb6fe6b1853f5cd9509bae34d3ad1e99be2b272a08e8564de7087` |
| Partition table | — | `6bfba02a02d830181cbaeb436302d62a413a156517c4f894367995d1ed812dce` |

- Free internal heap in the launcher: about 321 KB.

**Sparklet 1.1.0.**
- 1,765,408 B; built against the local platform components. The Sparklet repository records the final release hash.
- 60-second live run passed with 5 nodes and 0 errors.
- Minimum sampled heap 66,236 B; largest block 47,104 B.
- Spare stack: diagnostics 3,308 B, network 2,772 B, UI 13,740 B.

## Not exercised

- Manual BOOT + power-on download mode. esptool's automatic download-mode entry worked throughout.
- PWR 6-second power-off.
- Battery behaviour.
- Optical latency.
- Long-duration soak.
