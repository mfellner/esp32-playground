# Board support provenance

Moved from `mfellner/sparklet` (`firmware/sparkdash/components/board`, history preserved) on 2026-10-06.

- SDK: ESP-IDF v5.5.3, revision `2c211b236707889e8400c4dc5644dd5c4ee071e0`.
- Vendor: https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16 at `294543798f1a44e2f2c4d2976522323f2beee11d`. The reference was `02_Example/ESP-IDF-v5.5.3/09_LVGL_V9_Test`: display command table, reset sequence, orientation and touch configuration.
- Schematic: https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-2.16/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf, dated 2026-03-26.
  - J4: CS 15 and touch INT 5. The vendor's unused `user_config.h` macros reverse these.
  - LCD_RESET goes through R16 to ALDO3.
  - KEYS block: Key1 GPIO9 (BOOT), Key3 GPIO10 (KEY), Key2 PWRON. GPIO18 mirrors PWRON through a BSS138.

## PMIC writes (AXP2101, I2C 0x34)

1. **ALDO3:** set to 3.3 V (`0x94`), then its enable bit (`0x90` bit 2) is toggled to reset the panel.
2. **Power key:** `configure_power_key()`, added 2026-10-06.
   - `0x22`: long-press power-off enabled (bit 1 set), restart-instead-of-off disabled (bit 0 cleared). Other bits are preserved.
   - `0x27`: long-press IRQ after 1.5 s (bits 5:4 = 01), power-off after 6 s (bits 3:2 = 01). The power-on time is preserved.
   - `0x41`: short and long press IRQ enabled (bits 3 and 2).
   - `0x49`: those two status bits are cleared by writing 1.

Register meanings were cross-checked against XPowersLib (`src/REG/AXP2101Constants.h` and `src/XPowersAXP2101.hpp`), which the vendor bundles. AXP_IRQ is not routed to the ESP32, so status is polled. The PMIC keeps these registers across ESP32 resets, which is why every image writes the same values. Charger and unrelated rail registers are left untouched.

## QMI8658 and rotation

- The sensor is at I2C `0x6b` on SDA8/SCL7. WHO_AM_I must read `0x05`.
- Register settings: CTRL1 `0x60`, CTRL2 `0x07` (±2 g, 62.5 Hz), CTRL7 `0x01` (accelerometer only).
- Samples are read from `0x35` and scaled by 16384 LSB/g.
- Register facts come from the same vendor revision (`02_Example/Arduino-v3.3.3/02_I2C_QMI8658`).
- The SH8601 driver's axis swap is unsupported, so the board-owned panel wrapper rotates RGB565 stripes and rectangles in software. The pure helpers live in `orientation.cpp`, with host tests in `tests/host/orientation_tests.cpp`.

The upstream board repository has no top-level license grant in the inspected revision. The wiring facts and the required initialization sequence are attributed here. Managed dependencies keep their own licenses.
