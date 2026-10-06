#include "board.hpp"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "launcher.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include <cstring>

namespace launcher {
State state;
std::atomic<unsigned> key_presses{0}, boot_holds{0}, power_short{0}, power_long{0};
std::atomic<bool> key_pending{false};
static const char *TAG = "launcher";
static constexpr const char *Namespace = "launcher";

const char *reason_name(app_switch_launch_t kind) {
    switch (kind) {
    case APP_SWITCH_LAUNCH_REQUESTED:
        return "requested";
    case APP_SWITCH_LAUNCH_KEY:
        return "key";
    case APP_SWITCH_LAUNCH_CRASH:
        return "crash";
    case APP_SWITCH_LAUNCH_FALLBACK:
        return "fallback";
    default:
        return "normal";
    }
}

void storage_load() {
    // Never erase on init failure: the default NVS partition also holds other apps' settings.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable (%s); settings are not saved", esp_err_to_name(err));
        return;
    }
    state.storage = true;
    nvs_handle_t h;
    if (nvs_open(Namespace, NVS_READONLY, &h) != ESP_OK)
        return;
    size_t n = sizeof state.last;
    if (nvs_get_str(h, "last", state.last, &n) != ESP_OK)
        state.last[0] = 0;
    uint8_t b;
    if (nvs_get_u8(h, "bright", &b) == ESP_OK && b >= 10 && b <= 100)
        state.brightness = b;
    nvs_close(h);
}

static void save(const char *key, const char *text, int value) {
    nvs_handle_t h;
    if (!state.storage || nvs_open(Namespace, NVS_READWRITE, &h) != ESP_OK)
        return;
    esp_err_t err = text ? nvs_set_str(h, key, text) : nvs_set_u8(h, key, uint8_t(value));
    if (err == ESP_OK)
        err = nvs_commit(h);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "saving %s failed: %s", key, esp_err_to_name(err));
    nvs_close(h);
}
void storage_save_last(const char *label) {
    save("last", label, 0);
}
void storage_save_brightness(unsigned b) {
    save("bright", nullptr, int(b));
}
bool storage_erase_all() {
    nvs_flash_deinit();
    bool ok = nvs_flash_erase_partition("nvs") == ESP_OK;
    esp_err_t hermes = nvs_flash_erase_partition("nvs_hermes");
    ok &= hermes == ESP_OK || hermes == ESP_ERR_NOT_FOUND;
    ESP_LOGW(TAG, "all app settings erased: %s", ok ? "ok" : "failed");
    return ok;
}

// USB diagnostics: read-only STATUS; QA builds add TEST_BOOT <label>. Never a shell.
static void status() {
    char slots[320]{};
    size_t used = 0;
    for (size_t i = 0; i < state.count && used < sizeof slots; ++i) {
        const auto &s = state.slots[i];
        const char *st = s.state == APP_SWITCH_SLOT_VALID     ? "valid"
                         : s.state == APP_SWITCH_SLOT_INVALID ? "invalid"
                                                              : "empty";
        used += snprintf(slots + used, sizeof slots - used, "%s%s:%s:%s:%s%s%s", i ? "," : "",
                         s.label, st, s.project_name, s.version, s.running ? ":running" : "",
                         s.boot ? ":boot" : "");
    }
    ESP_LOGI("status",
             "LAUNCHER version=%s reason=%s reason_slot=%u last=%s slots=%s heap=%u min_heap=%u "
             "gpio9=%d gpio10=%d gpio18=%d key=%u boot_hold=%u pwr_short=%u pwr_long=%u",
             esp_app_get_description()->version, reason_name(state.reason.kind),
             unsigned(state.reason.slot), state.last, slots,
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)), gpio_get_level(GPIO_NUM_9),
             gpio_get_level(GPIO_NUM_10), gpio_get_level(GPIO_NUM_18), key_presses.load(),
             boot_holds.load(), power_short.load(), power_long.load());
}
static void diagnostics_task(void *) {
    char line[48]{};
    size_t used = 0;
    bool overflow = false;
    for (;;) {
        char c;
        if (usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(100)) != 1 || c == '\r')
            continue;
        if (c != '\n') {
            if (used < sizeof line - 1 && !overflow)
                line[used++] = c;
            else
                overflow = true;
            continue;
        }
        line[used] = 0;
        if (!overflow && !strcmp(line, "STATUS"))
            status();
#ifdef CONFIG_LAUNCHER_TEST_COMMANDS
        else if (!overflow && !strncmp(line, "TEST_BOOT ", 10))
            ESP_LOGI("qa_boot", "result=%s", esp_err_to_name(app_switch_boot(line + 10)));
#endif
        used = 0;
        overflow = false;
    }
}
void diagnostics_start() {
    // Probe inputs without internal pulls: GPIO9/10/18 all have 10K external pull-ups on this board,
    // and GPIO18 reads the PWR key through an inverting transistor (high while pressed).
    const gpio_config_t probe = {.pin_bit_mask = (1ULL << 9) | (1ULL << 10) | (1ULL << 18),
                                 .mode = GPIO_MODE_INPUT,
                                 .pull_up_en = GPIO_PULLUP_DISABLE,
                                 .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                 .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&probe));
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config{};
        config.rx_buffer_size = 128;
        config.tx_buffer_size = 256;
        ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    }
    assert(xTaskCreate(diagnostics_task, "launcher_diag", 3584, nullptr, 2, nullptr) == pdPASS);
}
} // namespace launcher

static void on_button(app_switch_button_t b, void *) {
    // esp_timer context: only flag work for the LVGL task.
    if (b == APP_SWITCH_BUTTON_KEY_SHORT) {
        launcher::key_presses++;
        launcher::key_pending = true;
    } else {
        launcher::boot_holds++;
    }
}

extern "C" void app_main() {
    using namespace launcher;
    state.reason = app_switch_take_launch_reason();
    ESP_LOGI("launcher", "BOOT version=%s reset=%d reason=%s slot=%u",
             esp_app_get_description()->version, esp_reset_reason(),
             reason_name(state.reason.kind), unsigned(state.reason.slot));
    if (state.reason.kind == APP_SWITCH_LAUNCH_CRASH ||
        state.reason.kind == APP_SWITCH_LAUNCH_FALLBACK) {
        // Keep power cycles in the launcher until the user picks a working app again.
        const esp_partition_t *self = esp_ota_get_running_partition();
        if (self && esp_ota_set_boot_partition(self) != ESP_OK)
            ESP_LOGW("launcher", "could not clear the boot selection");
    }
    storage_load();
    state.count = app_switch_list(state.slots, MaxSlots, true);
    board::init();
    diagnostics_start();
    ui_start();
    ESP_ERROR_CHECK(app_switch_buttons_start(on_button, nullptr));
    app_switch_mark_healthy();
}
