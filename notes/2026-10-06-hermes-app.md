# Hermes Gadget as the second platform app — 2026-10-06

`mfellner/hermes-gadget-sdk` was forked from `Adolanium/hermes-gadget-sdk` (main at `ffb1a43`, v0.2.0). It gained:
- a strip renderer for boards without PSRAM, plus a safe-area inset
- QR-code phone setup
- a board profile, `waveshare-esp32c6-touch-amoled-216`, that runs in this platform's `hermes` slot

Raw logs stay in ignored `logs/`.

## Install and isolation

- **Backup.** A fresh full backup (`backups/device-2026-10-06-1904.bin`, SHA-256 `0308792a…`) was taken before the first install.
- **Install.** `tools/device.py install hermes …/build/c6 --boot` wrote only 0x620000.
- **Boot log.** The platform bootloader loaded 0x620000. Every part came up: SH8601 480×480 in strips, both codecs, CST9217 touch, and the PMIC power key.
- **Isolation.** After setup and pairing, the shared default `nvs` partition (0x9000) was byte-identical to the backup, so Sparklet's settings were untouched.
- **First-boot erase of `nvs_hermes`.** That partition still held factory data from the vendor's old `assets` region, so the firmware erased it on first boot, as designed. The app only ever erases its own partition.

## Observations

**Display: rounded glass.** The first image cut off the corners of the top and bottom bars. The glass has large rounded corners. A 16 px inset (`DisplayInfo::inset`) fixed it; the user confirmed the layout fits.

**Phone setup.** The user found typed phone setup cumbersome. QR-code setup (a `WIFI:` code to join the setup network, then the page URL) worked on the first try. The encoder matches Nayuki's qrcodegen module for module; the rendered screens decode with zxing-cpp.

**Hermes.** v0.21.5, local git checkout, launchd gateway.
- The gadget plugin was installed from the fork and listens on `ws://192.168.178.31:8765/gadget`.
- Telegram stayed connected.
- `~/.hermes/config.yaml` was backed up to `~/.hermes/backups/config.yaml.before-gadget-2026-10-06`.
- The user approved the pairing.
- Spoken questions got transcribed, answered and spoken; the video shows the weather and a stock quote.

**Memory while connected.**
- 151.6 KB free internal heap; 142.6 KB minimum since boot; largest block 131 KB.
- Every task stack has at least 900 B spare.

**Microphone stall.**
- **Symptom:** sending audio blocked the app task for 150–500 ms about every 520 ms. With 20 ms microphone frames, the 48-event queue overflowed, and a send once hit its 2 s timeout.
- **Cause:** the Hermes host is a Mac on Wi-Fi with AWDL active. Its own pings to the router spiked to 237 ms. TCP ACKs arrived in bursts, and the socket stayed unwritable in between.
- **What did not help:** modem sleep off, 802.11n only, and `TCP_NODELAY`. All were measured.
- **Fix:** 100 ms microphone frames (the queue now holds ~4.8 s) plus a 16 KiB lwIP send buffer. Three scripted 4 s recordings with replies then showed zero dropped events, failed sends or speaker overflows.
- **Remaining effect:** the listening animation can still pause briefly during such stalls. A Hermes host on Ethernet, or with AirDrop/Handoff off, avoids this.

**Keys, after the user chose the mapping.**
- KEY held about 0.3 s talks; a quick press acts only where the screen asks for TALK.
- BOOT quick press cancels; held for 1 s it opens the launcher.
- PWR short press, read from the AXP2101 IRQ status, turns the screen off and on.
- The user confirmed all of these.

**Platform registration.** `slots.json` and the launcher now expect `hermes_gadget` in the `hermes` slot. The launcher was reinstalled.

## Not exercised

- The full hardware-validation checklist of the fork (`docs/hardware-validation.md`).
- PWR 6 s power-off.
- Battery-only operation.
- `wss://`.
- Long sessions.
- Image cards on the device (the simulator and host tests cover the strip image path).
- The crash guard with the Hermes app.
