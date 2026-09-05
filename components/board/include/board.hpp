#pragma once
#include "core.hpp"
#include "lvgl.h"
namespace board {
void init();
// LVGL lock owner only, except read-only diagnostic getters.
bool acceleration(spark::Acceleration &);
bool rotation_available();
spark::Orientation orientation();
bool set_orientation(spark::Orientation);
void brightness(unsigned percent);
#ifdef CONFIG_SPARKDASH_TEST_COMMANDS
void wait_transfer();
#endif
bool lock(int timeout_ms = -1);
void unlock();
using TouchFilter = bool (*)(bool down, int x, int y);
void set_touch_filter(TouchFilter);
} // namespace board
