# Hardware

## Identity

| Property | Value / evidence |
| --- | --- |
| Board | Waveshare ESP32-C6-Touch-AMOLED-2.16; factory log board label agrees |
| Processor | ESP32-C6 rev v0.2; boot reports 160 MHz |
| Flash | 16 MB independently confirmed by esptool; factory QIO/80 MHz, sparkDash generated flash arguments use DIO/80 MHz |
| USB | Espressif USB JTAG/serial debug unit, VID `0x303a`, PID `0x1001` |
| USB serial | `D4:05:92:B9:04:28` |
| Observed macOS port | `/dev/cu.usbmodem2101` on 2026-09-05 |
| Firmware | Multi-app platform since 2026-10-06: platform bootloader + launcher 0.1.0 (factory slot), Sparklet 1.1.0 (`sparklet` slot), ESP-IDF `v5.5.3`; original `01_Fac` image preserved in a verified full backup |
| Display / touch | 2.16 inch, 480 × 480; touch resolution confirmed in logs |
| Battery variant | Unknown; the listing offers versions with and without battery |

The [manufacturer overview](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.16) lists a CO5300 display controller over QSPI, CST9220 touch over I2C, QMI8658 IMU, PCF85063 RTC, AXP2101 power management, ES8311 audio codec, ES7210 audio ADC, dual microphones, and a TF/microSD slot. It specifies Wi-Fi 6, BLE 5, IEEE 802.15.4, 512 KB HP SRAM, and 16 KB LP SRAM. Battery connection is a 3.7 V MX1.25 header; check the actual unit before battery work.

## Driver naming discrepancy

Factory output says `sh8601: LCD panel create success, version: 2.0.1` and logs touch under `CST9217`, with chip type `0x9220`. These are software labels, not proof that the vendor component specifications are wrong. Use the board's own demo initialization and schematic when adding display/touch support. Do not substitute generic SH8601 wiring or another Waveshare board's pin map based on a log label.

## Pin assignments observed in factory logs

| Bus | Signal | GPIO |
| --- | --- | --- |
| I2C | SDA / SCL | 8 / 7 |
| I2S | MCLK / BCLK / WS | 19 / 20 / 22 |
| I2S | DIN / DOUT | 21 / 23 |

The boot log also states GPIO 17 and 16 are console UART I/O pins, without assigning each signal in that line. USB serial is the connection used here. Display QSPI, touch interrupt/reset, and the ALDO3 display reset have since been cross-checked and brought up as described below. SD and unrelated peripheral/button wiring remain outside this application’s validation. Consult the [schematic and source links](references.md) before using them.

## Schematic cross-check (2026-09-05)

The exact schematic and active vendor constructor confirm QSPI clock 0, data 1/2/3/4, **CS 15**, touch **INT 5**, and touch reset 11. LCD reset is driven through AXP2101 ALDO3. The vendor user_config.h reverses CS/INT but those macros are unused in the demo constructor. See the [Sparklet bring-up notes](https://github.com/mfellner/sparklet/blob/main/notes/2026-09-05-firmware-bringup.md). Flash size is now independently confirmed as 16 MB by esptool.

## Accelerometer bring-up (2026-09-06)

The custom firmware now reads the QMI8658 at `0x6b` on the existing SDA8/SCL7 bus;
identity `0x05` is required before configuration. With the user holding the device
in its original upright orientation, observed readings were approximately
`x=-0.026 g, y=+0.951 g, z=-0.138 g`. Sensor X/Y therefore feed the screen-plane
detector directly. The user confirmed readable output and aligned Settings/Back
and navigation controls on both sides and upside down. The detector rejects the
brief saturated readings observed immediately after sensor startup.

## Buttons (schematic 2026-03-26, observed 2026-10-06)

| Button | Wiring (schematic KEYS block) | Platform use | Observed |
| --- | --- | --- | --- |
| BOOT (Key1) | GPIO9, R8 10K pull-up, C14; strapping pin | Hold ≥1 s in an app → launcher (polled after startup only) | GPIO9 low while held; hold event and `SW_CPU` restart without download mode |
| KEY (Key3) | GPIO10, R18 10K pull-up, C22 not fitted; not a strapping pin | Short press → launcher / last app; held at reset → launcher once (bootloader) | `gpio10=0` while pressed; KEY events; `KEY held` bootloader log |
| PWR (Key2) | AXP2101 PWRON via RP7 510R; PWRON also drives BSS138 T1 (gate via R28), whose drain is GPIO18 with R11 10K pull-up | Short press → app-defined (screen off/on, dim); 6 s hold → PMIC power-off | AXP2101 INTSTS2 short-press events counted; GPIO18 level change not captured in sampled STATUS |

There is no RESET button; CHIP_PU is driven by the AXP2101 PWROK output with R9/C15. AXP_IRQ (PMIC pin 38) is not routed to an ESP32 GPIO, so firmware polls INTSTS2 (`0x49`) over I2C. The schematic pin table labels GPIO10 `LCD_RESET`, but J4 routes LCD_RESET through R16 to ALDO3 and the KEYS block puts Key3 on GPIO10; the observed KEY behaviour matches the KEYS block.

The PMIC keeps power-key settings across ESP32 resets. The platform board component writes them explicitly on every start: long-press power-off enabled at 6 s, long-press IRQ at 1.5 s, short/long IRQs enabled (see `components/board/PROVENANCE.md`). PWR 6 s power-off itself has not been exercised.
