# References

Checked on 2026-09-05. Prefer exact-board manufacturer resources and primary Espressif documentation.

- [Waveshare board overview](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.16): specifications, board resources, and navigation to development guides.
- [Waveshare resources](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.16/Resources-And-Documents): schematic, mechanical files, component datasheets, demos.
- [Board schematic PDF](https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-2.16/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf): wiring authority; linked but not yet inspected locally.
- [Official Waveshare source](https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16): example code; not yet cloned or built.
- [Espressif esptool commands](https://docs.espressif.com/projects/esptool/en/latest/esp32c6/esptool/basic-commands.html): flash ID, MAC, backup, and image operations.
- [ESP-IDF ESP32-C6 documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/index.html): SDK entry point; select the version used by the chosen example.
- [Original AliExpress listing](https://de.aliexpress.com/item/1005012572456336.html): user-provided purchase reference. Automated retrieval failed; the pasted listing identifies Waveshare, ESP32-C6, and 2.16 inch 480 × 480 AMOLED. Its search header mentions ESP32-S3 1.75 inch, which is not the purchased item title.

Do not assume moving `main`, `latest`, or `stable` URLs are reproducible version pins. Record a commit/tag when adopting source or a toolchain.
