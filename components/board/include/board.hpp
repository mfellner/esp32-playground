#pragma once
#include "lvgl.h"
namespace board {
void init();
void brightness(unsigned percent);
bool lock(int timeout_ms=-1);
void unlock();
using TouchFilter=bool(*)(bool down,int x,int y);
void set_touch_filter(TouchFilter);
}
