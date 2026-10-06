// Shared RTC handoff record between the platform bootloader, the launcher and apps.
// Lives in rtc_retain_mem_t.custom (CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE = 16 bytes),
// excluded from the retain-memory CRC, cleared by the bootloader after power loss.
#pragma once
#include <stdint.h>

#define APP_SWITCH_RTC_MAGIC 0x31575341u // "ASW1"
#define APP_SWITCH_NO_SLOT 0xffu         // launcher (factory) or unknown
#define APP_SWITCH_CRASH_LIMIT 3         // unhealthy boots of one slot before the launcher takes over

typedef enum {
    APP_SWITCH_REQUEST_NONE = 0,
    APP_SWITCH_REQUEST_OPEN_LAUNCHER = 1, // an app asked for the launcher
    APP_SWITCH_REQUEST_SWITCH = 2,        // deliberate restart into a selected slot
} app_switch_request_t;

typedef enum {
    APP_SWITCH_FALLBACK_NONE = 0,
    APP_SWITCH_FALLBACK_KEY = 1,   // bootloader saw the recovery key at boot
    APP_SWITCH_FALLBACK_CRASH = 2, // bootloader crash guard tripped
} app_switch_fallback_t;

typedef struct {
    uint32_t magic;
    uint8_t request;      // app_switch_request_t, consumed by the next boot
    uint8_t from_slot;    // OTA index of the requesting app
    uint8_t crash_count;  // consecutive unhealthy boots of crash_slot (bootloader)
    uint8_t crash_slot;   // OTA index tracked by the crash guard
    uint8_t fallback;     // app_switch_fallback_t set by the bootloader
    uint8_t fallback_slot; // slot that was skipped by the fallback
    uint8_t boot_pending;  // bootloader started crash_slot; cleared by app_switch_mark_healthy()
    uint8_t reserved[5];
} app_switch_rtc_t;

_Static_assert(sizeof(app_switch_rtc_t) == 16, "must match CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE");
