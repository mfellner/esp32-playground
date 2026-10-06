// Board support for the Waveshare ESP32-C6-Touch-AMOLED-2.16: panel, touch, LVGL, IMU and PMIC keys.
#pragma once
#include "board_orientation.hpp"
#include "lvgl.h"
namespace board {
void init();
// LVGL lock owner only, except read-only diagnostic getters.
bool acceleration(Acceleration &);
bool rotation_available();
Orientation orientation();
bool set_orientation(Orientation);
void brightness(unsigned percent);
// Drains queued panel transfers (LVGL task / lock owner only).
void wait_transfer();
bool lock(int timeout_ms = -1);
void unlock();
using TouchFilter = bool (*)(bool down, int x, int y);
void set_touch_filter(TouchFilter);
// Optional observer for every accepted IMU sample (e.g. QA logging); called by the LVGL lock owner.
using AccelerationObserver = void (*)(const Acceleration &);
void set_acceleration_observer(AccelerationObserver);
// AXP2101 PWR key events since the last call (I2C read; poll at ~20 Hz from the LVGL task).
enum PowerKey : unsigned { PowerKeyNone = 0, PowerKeyShort = 1, PowerKeyLong = 2 };
unsigned poll_power_key();
} // namespace board
