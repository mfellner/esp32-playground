# Interacting with the board

## Discover and monitor

```sh
uv run scripts/esp32_serial.py list
uv run scripts/esp32_serial.py monitor --seconds 5
```

The helper selects USB serial `D4:05:92:B9:04:28` and Espressif VID/PID `303a:1001`. An explicit `--port` overrides selection. It uses 115200 baud, sets DTR and RTS false before opening, reads for a bounded duration, and closes the connection. This setup produced usable boot logs during initial discovery, but also appears to have rebooted the device (`USB_UART_HPSYS`). It is not guaranteed reset-free.

The USB descriptor alone identifies an Espressif interface, not the exact board. Confirm the ESP32-C6 and board-specific firmware labels in logs. Do not send shell commands to the factory app: no command interpreter or screen-control protocol has been established.

On macOS, additional discovery is available with:

```sh
ls /dev/cu.*
ioreg -p IOUSB -l -w 0 | rg 'USB Vendor Name|USB Product Name|USB Serial Number|idVendor|idProduct'
lsof /dev/cu.usbmodem2101
```

`system_profiler SPUSBDataType` returned no useful output during discovery; `ioreg` did. If busy, close the existing monitor before reopening. If missing, check power, use a data-capable cable, reconnect, and enumerate again.

## Bootloader inspection and backup

The pinned esptool CLI below was downloaded and its version command tested during setup. **The device commands in this section have not yet been run.** They normally reset the device into its bootloader and back. Stop any serial monitor first and replace the example port with the discovered one.

```sh
uvx --from esptool==5.4.0 esptool --chip esp32c6 --port /dev/cu.usbmodem2101 flash-id
uvx --from esptool==5.4.0 esptool --chip esp32c6 --port /dev/cu.usbmodem2101 read-mac
```

Before replacing factory firmware, save a full flash image to a new filename:

```sh
mkdir -p backups
uvx --from esptool==5.4.0 esptool --chip esp32c6 --port /dev/cu.usbmodem2101 read-flash 0 ALL backups/factory-2026-09-05.bin
shasum -a 256 backups/factory-2026-09-05.bin
wc -c backups/factory-2026-09-05.bin
```

For the reported 16 MB flash, expect 16,777,216 bytes. Preserve the checksum separately with capture date, board identity, and tool version. Use a fresh name for subsequent captures. Flash dumps can contain configuration and credentials, so `backups/` is ignored. No backup exists yet. If security restrictions prevent reading, report that limitation before replacing the only available firmware; do not bypass protection.

If automatic bootloader entry fails, the vendor describes holding BOOT while powering on. Re-enumerate the port after entering download mode. A battery-powered unit may remain powered when USB is unplugged; use its power control if necessary. Avoid repeated reset loops when manual board access is required.

Command syntax and full-flash reads are documented in [Espressif's esptool guide](https://docs.espressif.com/projects/esptool/en/latest/esp32c6/esptool/basic-commands.html).

## Firmware development

Start from the [exact Waveshare board repository](https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16), inspect its example requirements, and pin the chosen revision and framework version. ESP-IDF 5.5.3 is an observed factory build version, not a verified requirement for every vendor example. No ESP-IDF/Arduino/PlatformIO toolchain has been installed here.

For an ESP-IDF project, after installing and activating its supported SDK, the usual workflow is:

```sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/cu.usbmodem2101 flash monitor
```

These commands require a real ESP-IDF project; the repository root is not one yet. Flash only as part of a requested firmware change, after backup. Let the build generate offsets and partition settings. The observed factory partition layout is historical information, not a flashing recipe. Exit the IDF monitor with Ctrl+].

For host-driven display or sensor control, implement and document a serial protocol in custom firmware (for example, framed requests and responses). Mere access to the USB console does not expose display, microphones, touch, or GPIO as host peripherals.
