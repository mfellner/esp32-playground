# First local contact — 2026-09-05

The board was discovered on macOS at `/dev/cu.usbmodem2101`, with USB serial `D4:05:92:B9:04:28`. A five-second PySerial read at 115200 baud received 6,178 bytes. No application bytes were sent and no firmware was written. Despite setting DTR/RTS false before opening, the captured startup reports a USB UART reset; treat opening as potentially disruptive.

Selected excerpts from the actual capture (not a complete raw log):

```text
ESP-ROM:esp32c6-20220919
rst:0x15 (USB_UART_HPSYS),boot:0x7f (SPI_FAST_FLASH_BOOT)
boot: chip revision: v0.2
boot.esp32c6: SPI Speed      : 80MHz
boot.esp32c6: SPI Mode       : QIO
boot.esp32c6: SPI Flash Size : 16MB
app_init: Project name:     01_Fac
app_init: App version:      1
app_init: Compile time:     Jul 22 2026 19:15:00
app_init: ESP-IDF:          v5.5.3
axp2101: Init PMU SUCCESS!
sh8601: LCD panel create success, version: 2.0.1
CST9217: Resolution X: 480, Y: 480
CST9217: Chip Type: 0x9220, ProjectID: 0x542F
esp32_c6_touch_amoled_2.16: Backlight on
Main: [main.cpp:0090](app_main): Using stylesheet (480x480 Dark)
BS:Core: [esp_brookesia_base_context.cpp:0189](begin): Library version: 0.6.0
main_task: Returned from app_main()
```

Observed partitions:

| Label | Offset | Length |
| --- | --- | --- |
| nvs | 0x9000 | 0x6000 |
| otadata | 0xf000 | 0x2000 |
| phy_init | 0x11000 | 0x1000 |
| ota_0 | 0x20000 | 0x600000 |
| ota_1 | 0x620000 | 0x300000 |
| assets | 0x920000 | 0x300000 |
| storage | 0xc20000 | 0x300000 |

The bootloader loaded the app at `0x20000`. This table describes factory firmware, not a new project's required layout.

Warnings/errors included `sdmmc_card_init failed (0x107)`, an I2C pull-up warning, disabled LVGL gesture recognition, and `GPIO isr service already installed`. SD card presence was not checked, and these messages alone do not establish a hardware fault. PMU, touch, RTC, IMU, and audio initialization continued.

Not yet verified: visual screen contents, interactive serial commands, Wi-Fi/BLE, audio operation, SD card, battery variant, flash backup, bootloader probing, or custom firmware compilation/flashing.

## Repository setup verification

The committed serial helper successfully enumerated the same USB identity and captured 4,590 bytes in a bounded two-second session to ignored `logs/setup-verification.bin`. This second opening again produced `USB_UART_HPSYS` boot output. The helper closed the port afterward. PySerial is pinned to 3.5; esptool 5.4.0 was fetched into uv's tool cache and its version command succeeded without contacting the bootloader. The custom skill passed the skill-creator validator and was installed by symlink under `~/.codex/skills/esp32-c6-waveshare`.
