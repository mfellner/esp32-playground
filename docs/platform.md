# Multi-app platform

## Boot flow

```text
ROM ─▶ platform bootloader (firmware/launcher/bootloader_components/main)
        │  1. reads partition table + otadata  → selected slot (blank otadata = launcher)
        │  2. selected slot is an OTA app and KEY (GPIO10) is held  → launcher (once)
        │  3. that app started APP_SWITCH_CRASH_LIMIT (3) times without
        │     app_switch_mark_healthy()                              → launcher (once)
        ▼
     app image ── app_switch_open_launcher()  → erases otadata, RTC request, esp_restart
     launcher  ── app_switch_boot(label)      → verifies image hash, writes otadata, esp_restart
```

**Switching mechanism.** Switching uses stock ESP-IDF OTA selection: `esp_ota_set_boot_partition()` followed by `esp_restart()`. Selecting the factory slot erases otadata, so the launcher stays selected after a power cycle. Starting an app from the launcher selects it again.

**Bootloader fallback.** If a selected image is invalid, the bootloader falls back to the next lower slot. That means a damaged `hermes` image boots `sparklet`, not the launcher. The launcher reports a fallback when the selected and running slots differ.

**RTC handoff.** A 16-byte `app_switch_rtc_t` sits in `rtc_retain_mem_t.custom` (`CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE=0x10`) and is excluded from the retain-memory CRC. It carries:
- the open-launcher request, plus the slot that made it
- the bootloader's fallback reason (KEY or crash)
- the crash counter

Power loss clears it.

**Restarts don't enter download mode.** `esp_restart()` is a CPU software reset that doesn't re-sample strapping pins, so holding BOOT during a switch can't enter download mode. This was observed on the device: `rst:0xc (SW_CPU),boot:0x7f`.

## Platform contract

The contract lives in `components/app_switch/layout/sdkconfig.defaults.platform` and is enforced by `platform_check_layout()`. Every image on the device must use:
- ESP-IDF 5.5.3 for the ESP32-C6
- 16 MB flash in DIO at 80 MHz
- partition table at 0x8000 with MD5, byte-identical `partitions.csv`
- no app rollback, factory-reset or test-app bootloader options
- `BOOTLOADER_CUSTOM_RESERVE_RTC=y` with size `0x10`, excluded from the CRC
- no secure boot or flash encryption

The app linker reserves the same RTC area as the bootloader, so these options must match in every image.

## Adding an app

1. **Layout files.** Copy `components/app_switch/layout/partitions.csv` and `sdkconfig.defaults.platform` into the project unchanged. Apply them in `CMakeLists.txt`:

   ```cmake
   set(SDKCONFIG_DEFAULTS "sdkconfig.defaults.platform;sdkconfig.defaults")
   include($ENV{IDF_PATH}/tools/cmake/project.cmake)
   project(myapp)
   platform_app_slot(hermes)   # the app's slot label from slots.json
   ```

2. **Dependencies.** Add them to `main/idf_component.yml`, then refer to them as `mfellner__app_switch` (and `mfellner__board`) in `REQUIRES`:

   ```yaml
   mfellner/app_switch:
     git: https://github.com/mfellner/esp32-playground.git
     path: components/app_switch
     version: v0.1.0
   mfellner/board:              # optional: LVGL board support
     git: https://github.com/mfellner/esp32-playground.git
     path: components/board
     version: v0.1.0
   ```

   For local development, use `override_path` instead of `git`/`version`, and don't commit it.

3. **App code.**
   - Call `app_switch_buttons_start(NULL, NULL)` early; it uses a 20 ms `esp_timer` and no task.
   - Call `app_switch_mark_healthy()` once the app is up.
   - Offer a visible control that calls `app_switch_open_launcher()`.
   - Poll `board::poll_power_key()` if the app uses PWR.

4. **Storage.**
   - Use your own NVS namespace, or a dedicated NVS partition (`nvs_hermes` for the reserved slot).
   - Never call `nvs_flash_erase()` on the shared `nvs` partition.
   - Don't erase it when `nvs_flash_init()` fails.

5. **OTA.**
   - Never use `esp_ota_get_next_update_partition()`; it would pick another app's slot.
   - Target your own label only. Because an app can't overwrite its running image, update over USB (`device.py install`) or stage the image in `storage`.

6. **Register the slot.** Add the slot's `project_name` to `slots.json` and to `expected_project()` in `firmware/launcher/main/slot_model.cpp`. The launcher and `device.py boot` refuse images that don't match.

## Tools

| Command | Effect |
| --- | --- |
| `tools/device.py status` | Shows the partition table, installed images and the next boot (read-only) |
| `tools/device.py backup` | Full 16 MiB read, with SHA-256 and metadata JSON |
| `tools/device.py migrate` | One-time move from the single-app layout. Requires a verified backup, writes the launcher's generated images plus app slots, and checks that NVS is unchanged |
| `tools/device.py install <label> <build>` | Writes one slot from `<label>-flash_args` (or the launcher's `flasher_args.json`). Refuses if the device table differs from the build's |
| `tools/device.py boot <label>` / `recover` | Rewrites otadata in the same way as `esp_rewrite_ota_data()`, or erases it |
| `tools/device.py erase <label>` | Erases an OTA slot that isn't selected for boot |
| `tools/check_switch.py` | Bounded USB switch round trips with QA builds |
| `scripts/esp32_serial.py monitor --send STATUS --repeat 2` | Bounded log capture, optionally sending firmware diagnostic lines |

The launcher's USB diagnostic `STATUS` reports:
- the slots, the launch reason and the last app
- heap
- raw GPIO9/10/18 levels
- button counters

QA launcher builds (`CONFIG_LAUNCHER_TEST_COMMANDS`) add `TEST_BOOT <label>`.

## Known limits

- A switch is a full reboot. Starting Sparklet from the launcher took about 1 s to its first log line, and returning took about 0.6 s, measured over USB rather than optically.
- The crash guard only covers apps that fail before `app_switch_mark_healthy()`. Later crash loops are not detected.
- The `storage` partition still holds factory data from the vendor layout. Erase it before first use.
- The launcher has no auto-rotation. It is laid out for the upright orientation.
