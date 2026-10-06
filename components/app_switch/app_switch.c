#include "app_switch.h"
#include "app_switch_rtc.h"
#include "bootloader_common.h"
#include "driver/gpio.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <string.h>

#if !CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC || CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE != 16
#error "Include app_switch/layout/sdkconfig.defaults.platform: the RTC handoff area must be 16 bytes"
#endif

static const char *TAG = "app_switch";

static app_switch_rtc_t *rtc(void) {
    app_switch_rtc_t *r = (app_switch_rtc_t *)bootloader_common_get_rtc_retain_mem()->custom;
    if (r->magic != APP_SWITCH_RTC_MAGIC) {
        memset(r, 0, sizeof *r);
        r->magic = APP_SWITCH_RTC_MAGIC;
        r->from_slot = r->crash_slot = r->fallback_slot = APP_SWITCH_NO_SLOT;
    }
    return r;
}

static uint8_t ota_index(const esp_partition_t *p) {
    if (p && p->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
        p->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX)
        return p->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN;
    return APP_SWITCH_NO_SLOT;
}

const char *app_switch_label(uint8_t index) {
    if (index == APP_SWITCH_NO_SLOT)
        return NULL;
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_MIN + index, NULL);
    return p ? p->label : NULL;
}

size_t app_switch_list(app_switch_slot_t *slots, size_t max, bool verify) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    size_t n = 0;
    esp_partition_iterator_t it =
        esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it && n < max; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        app_switch_slot_t *s = &slots[n++];
        memset(s, 0, sizeof *s);
        strlcpy(s->label, p->label, sizeof s->label);
        s->ota_index = ota_index(p);
        s->offset = p->address;
        s->size = p->size;
        s->running = running && running->address == p->address;
        s->boot = boot && boot->address == p->address;
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(p, &desc) != ESP_OK) {
            s->state = APP_SWITCH_SLOT_EMPTY;
            continue;
        }
        strlcpy(s->project_name, desc.project_name, sizeof s->project_name);
        strlcpy(s->version, desc.version, sizeof s->version);
        s->state = APP_SWITCH_SLOT_VALID;
        if (verify && !s->running) {
            const esp_partition_pos_t pos = {.offset = p->address, .size = p->size};
            esp_image_metadata_t meta;
            if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta) != ESP_OK)
                s->state = APP_SWITCH_SLOT_INVALID;
        }
    }
    esp_partition_iterator_release(it);
    return n;
}

static void restart(app_switch_request_t request) {
    app_switch_rtc_t *r = rtc();
    r->request = request;
    r->from_slot = ota_index(esp_ota_get_running_partition());
    esp_restart();
}

esp_err_t app_switch_boot(const char *label) {
    if (!label)
        return ESP_ERR_INVALID_ARG;
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!p)
        return ESP_ERR_NOT_FOUND;
    // Verifies the image hash for OTA slots; selecting the factory slot erases otadata.
    esp_err_t err = esp_ota_set_boot_partition(p);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot select %s: %s", label, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "restarting into %s", label);
    restart(p->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY ? APP_SWITCH_REQUEST_OPEN_LAUNCHER
                                                            : APP_SWITCH_REQUEST_SWITCH);
    return ESP_FAIL;
}

esp_err_t app_switch_open_launcher(void) {
    return app_switch_boot(APP_SWITCH_LAUNCHER_LABEL);
}

void app_switch_mark_healthy(void) {
    app_switch_rtc_t *r = rtc();
    r->crash_count = 0;
    r->boot_pending = 0;
    r->request = APP_SWITCH_REQUEST_NONE;
}

app_switch_launch_reason_t app_switch_take_launch_reason(void) {
    app_switch_rtc_t *r = rtc();
    app_switch_launch_reason_t reason = {APP_SWITCH_LAUNCH_NORMAL, APP_SWITCH_NO_SLOT};
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (r->fallback == APP_SWITCH_FALLBACK_KEY)
        reason = (app_switch_launch_reason_t){APP_SWITCH_LAUNCH_KEY, r->fallback_slot};
    else if (r->fallback == APP_SWITCH_FALLBACK_CRASH)
        reason = (app_switch_launch_reason_t){APP_SWITCH_LAUNCH_CRASH, r->fallback_slot};
    else if (r->request == APP_SWITCH_REQUEST_OPEN_LAUNCHER)
        reason = (app_switch_launch_reason_t){APP_SWITCH_LAUNCH_REQUESTED, r->from_slot};
    else if (boot && running && boot->address != running->address)
        reason = (app_switch_launch_reason_t){APP_SWITCH_LAUNCH_FALLBACK, ota_index(boot)};
    r->request = APP_SWITCH_REQUEST_NONE;
    r->fallback = APP_SWITCH_FALLBACK_NONE;
    r->fallback_slot = APP_SWITCH_NO_SLOT;
    return reason;
}

// Buttons: polled every 20 ms from an esp_timer so apps pay no task stack.
typedef struct {
    int gpio;
    bool armed;   // a release has been seen since startup (ignores keys held through a restart)
    bool pressed;
    bool fired;
    int64_t since;
} button_t;

static button_t boot_button = {.gpio = CONFIG_APP_SWITCH_BOOT_GPIO};
static button_t key_button = {.gpio = CONFIG_APP_SWITCH_KEY_GPIO};
static app_switch_button_cb_t button_cb;
static void *button_arg;
static esp_timer_handle_t button_timer;

static void emit(app_switch_button_t b) {
    ESP_LOGI(TAG, "button %s", b == APP_SWITCH_BUTTON_KEY_SHORT ? "KEY" : "BOOT hold");
    if (button_cb)
        button_cb(b, button_arg);
    else
        app_switch_open_launcher();
}

static bool sample(button_t *b, int64_t now_ms) {
    if (b->gpio < 0)
        return false;
    bool down = gpio_get_level(b->gpio) == 0;
    if (!b->armed) {
        b->armed = !down;
        return false;
    }
    if (down && !b->pressed) {
        b->pressed = true;
        b->fired = false;
        b->since = now_ms;
    } else if (!down && b->pressed) {
        b->pressed = false;
        return true; // released
    }
    return false;
}

static void poll_buttons(void *unused) {
    const int64_t now = esp_timer_get_time() / 1000;
    if (sample(&key_button, now)) {
        const int64_t held = now - key_button.since;
        if (held >= 30 && held <= CONFIG_APP_SWITCH_KEY_MAX_PRESS_MS)
            emit(APP_SWITCH_BUTTON_KEY_SHORT);
    }
    sample(&boot_button, now);
    if (boot_button.pressed && !boot_button.fired &&
        now - boot_button.since >= CONFIG_APP_SWITCH_BOOT_HOLD_MS) {
        boot_button.fired = true;
        emit(APP_SWITCH_BUTTON_BOOT_HOLD);
    }
}

esp_err_t app_switch_buttons_start(app_switch_button_cb_t cb, void *arg) {
    if (button_timer)
        return ESP_ERR_INVALID_STATE;
    button_cb = cb;
    button_arg = arg;
    uint64_t mask = 0;
    if (boot_button.gpio >= 0)
        mask |= 1ULL << boot_button.gpio;
    if (key_button.gpio >= 0)
        mask |= 1ULL << key_button.gpio;
    if (!mask)
        return ESP_OK;
    const gpio_config_t io = {
        .pin_bit_mask = mask, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK)
        return err;
    const esp_timer_create_args_t args = {.callback = poll_buttons, .name = "app_switch_keys"};
    err = esp_timer_create(&args, &button_timer);
    return err == ESP_OK ? esp_timer_start_periodic(button_timer, 20 * 1000) : err;
}
