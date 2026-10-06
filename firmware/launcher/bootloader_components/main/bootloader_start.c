/*
 * Platform bootloader for the Waveshare ESP32-C6-Touch-AMOLED-2.16 multi-app layout.
 * Derived from ESP-IDF 5.5.3 examples/custom_bootloader/bootloader_override (Apache-2.0,
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD).
 *
 * Additions before the stock image load:
 *  - KEY (GPIO10, active low, 10K pull-up, not a strapping pin) held at boot starts the launcher
 *    once without changing otadata.
 *  - Crash guard: an OTA app that is started APP_SWITCH_CRASH_LIMIT times in a row without calling
 *    app_switch_mark_healthy() is skipped once in favour of the launcher.
 */
#include <stdbool.h>
#include <sys/reent.h>
#include "app_switch_rtc.h"
#include "bootloader_common.h"
#include "bootloader_config.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "hal/gpio_ll.h"
#include "sdkconfig.h"

#define PLATFORM_KEY_GPIO 10

static const char *TAG = "boot";

static bool key_held(void) {
    esp_rom_gpio_pad_select_gpio(PLATFORM_KEY_GPIO);
    gpio_ll_input_enable(&GPIO, PLATFORM_KEY_GPIO);
    esp_rom_gpio_pad_pullup_only(PLATFORM_KEY_GPIO);
    esp_rom_delay_us(1000);
    for (int i = 0; i < 10; ++i) { // ~50 ms of consistent low samples
        if (gpio_ll_get_level(&GPIO, PLATFORM_KEY_GPIO))
            return false;
        esp_rom_delay_us(5000);
    }
    return true;
}

static app_switch_rtc_t *handoff(void) {
    // A power-on leaves the retain area invalid; stock code would zero it after we write to it.
    if (bootloader_common_get_rtc_retain_mem_reboot_counter() == 0) {
        bootloader_common_reset_rtc_retain_mem();
        bootloader_common_update_rtc_retain_mem(NULL, true);
    }
    app_switch_rtc_t *r = (app_switch_rtc_t *)bootloader_common_get_rtc_retain_mem()->custom;
    if (r->magic != APP_SWITCH_RTC_MAGIC) {
        *r = (app_switch_rtc_t){.magic = APP_SWITCH_RTC_MAGIC,
                                .from_slot = APP_SWITCH_NO_SLOT,
                                .crash_slot = APP_SWITCH_NO_SLOT,
                                .fallback_slot = APP_SWITCH_NO_SLOT};
    }
    return r;
}

static int platform_select(int index) {
    app_switch_rtc_t *r = handoff();
    if (index < 0) { // launcher (factory) or test app: nothing to guard
        r->boot_pending = 0;
        return index;
    }
    if (key_held()) {
        ESP_LOGW(TAG, "KEY held: starting launcher instead of OTA app %d", index);
        r->fallback = APP_SWITCH_FALLBACK_KEY;
        r->fallback_slot = (uint8_t)index;
        r->boot_pending = 0;
        return FACTORY_INDEX;
    }
    if (r->crash_slot != (uint8_t)index) {
        r->crash_slot = (uint8_t)index;
        r->crash_count = 0;
    } else if (r->boot_pending && r->crash_count < 0xff) {
        r->crash_count++;
    }
    if (r->crash_count >= APP_SWITCH_CRASH_LIMIT) {
        ESP_LOGE(TAG, "OTA app %d failed to start %d times: starting launcher", index,
                 r->crash_count);
        r->fallback = APP_SWITCH_FALLBACK_CRASH;
        r->fallback_slot = (uint8_t)index;
        r->crash_count = 0;
        r->boot_pending = 0;
        return FACTORY_INDEX;
    }
    r->boot_pending = 1;
    return index;
}

static int select_partition_number(bootloader_state_t *bs) {
    if (!bootloader_utility_load_partition_table(bs)) {
        ESP_LOGE(TAG, "load partition table error!");
        return INVALID_INDEX;
    }
    return platform_select(bootloader_utility_get_selected_boot_partition(bs));
}

void __attribute__((noreturn)) call_start_cpu0(void) {
    if (bootloader_init() != ESP_OK) {
        bootloader_reset();
    }
#ifdef CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP
    bootloader_utility_load_boot_image_from_deep_sleep();
#endif
    bootloader_state_t bs = {0};
    int boot_index = select_partition_number(&bs);
    if (boot_index == INVALID_INDEX) {
        bootloader_reset();
    }
    bootloader_utility_load_boot_image(&bs, boot_index);
}

#if CONFIG_LIBC_NEWLIB
// Return global reent struct if any newlib functions are linked to bootloader
struct _reent *__getreent(void) {
    return _GLOBAL_REENT;
}
#endif
