// Launcher client for apps that share the platform flash layout.
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SWITCH_LAUNCHER_LABEL "launcher"

typedef enum {
    APP_SWITCH_SLOT_EMPTY,   // no app image header
    APP_SWITCH_SLOT_VALID,   // image header present (and verified when requested)
    APP_SWITCH_SLOT_INVALID, // header present but verification failed
} app_switch_slot_state_t;

typedef struct {
    char label[17];
    uint8_t ota_index; // APP_SWITCH_NO_SLOT for the factory launcher
    uint32_t offset, size;
    app_switch_slot_state_t state;
    char project_name[32], version[32];
    bool running; // this image is executing now
    bool boot;    // otadata selects this image for the next boot
} app_switch_slot_t;

typedef enum {
    APP_SWITCH_LAUNCH_NORMAL,    // power-on / blank otadata / host recover
    APP_SWITCH_LAUNCH_REQUESTED, // an app called app_switch_open_launcher()
    APP_SWITCH_LAUNCH_KEY,       // recovery key held at boot (platform bootloader)
    APP_SWITCH_LAUNCH_CRASH,     // crash guard fell back to the launcher
    APP_SWITCH_LAUNCH_FALLBACK,  // selected image was invalid; bootloader fell back
} app_switch_launch_t;

typedef struct {
    app_switch_launch_t kind;
    uint8_t slot; // requesting/failed OTA index, APP_SWITCH_NO_SLOT if unknown
} app_switch_launch_reason_t;

typedef enum {
    APP_SWITCH_BUTTON_KEY_SHORT, // KEY pressed and released quickly
    APP_SWITCH_BUTTON_BOOT_HOLD, // BOOT held for CONFIG_APP_SWITCH_BOOT_HOLD_MS
} app_switch_button_t;

typedef void (*app_switch_button_cb_t)(app_switch_button_t button, void *arg);

// Lists app partitions in table order. With verify, every image's hash is checked (slow: ~0.1 s/MiB).
size_t app_switch_list(app_switch_slot_t *slots, size_t max, bool verify);
// Selects the labelled app for this and later boots and restarts. Returns only on failure.
esp_err_t app_switch_boot(const char *label);
// Clears the boot selection so the device restarts into the launcher. Returns only on failure.
esp_err_t app_switch_open_launcher(void);
// Tells the crash guard that the running app started successfully.
void app_switch_mark_healthy(void);
// Why the running image was started; consumes the pending request. Call once, early.
app_switch_launch_reason_t app_switch_take_launch_reason(void);
// Polls KEY/BOOT from an esp_timer (no extra task). The callback runs in the esp_timer task and must be
// short. A NULL callback opens the launcher on either gesture.
esp_err_t app_switch_buttons_start(app_switch_button_cb_t cb, void *arg);
// Label of an OTA index from the partition table, or NULL.
const char *app_switch_label(uint8_t ota_index);

#ifdef __cplusplus
}
#endif
