#pragma once
#include "app_switch.h"
#include <atomic>
#include <cstddef>

namespace launcher {
constexpr size_t MaxSlots = 8;
struct State {
    app_switch_slot_t slots[MaxSlots];
    size_t count = 0;
    app_switch_launch_reason_t reason{};
    char last[17]{};
    unsigned brightness = 60;
    bool storage = false; // NVS usable
};
extern State state;
// Button/diagnostic counters, written outside the LVGL task.
extern std::atomic<unsigned> key_presses, boot_holds, power_short, power_long;
extern std::atomic<bool> key_pending;

void storage_load();
void storage_save_last(const char *label);
void storage_save_brightness(unsigned);
// Erases every NVS partition of the platform layout. Affects all apps.
bool storage_erase_all();

void ui_start();
void diagnostics_start();
const char *reason_name(app_switch_launch_t);
} // namespace launcher
