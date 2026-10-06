// Pure display-orientation helpers for the 480 x 480 panel; host-testable (no ESP-IDF dependencies).
#pragma once
#include <cstdint>

namespace board {
enum class Orientation : uint8_t { Upright, Clockwise90, UpsideDown, Clockwise270 };
struct Point {
    int x, y;
};
struct Rect {
    int x1, y1, x2, y2;
}; // exclusive upper bounds
struct Acceleration {
    float x, y, z;
}; // g; x right, y down in upright display coordinates
Point rotate_point(Point, Orientation, int side = 480);
Point unrotate_point(Point, Orientation, int side = 480);
Rect rotate_rect(Rect, Orientation, int side = 480);
// Non-overlapping tightly packed buffers; preserves both bytes of every RGB565 pixel.
void rotate_pixels(const uint16_t *, uint16_t *, int width, int height, Orientation);
struct OrientationDetector {
    Orientation current = Orientation::Upright, candidate = Orientation::Upright;
    uint64_t since = 0, last_sample = 0;
    bool pending = false;
    Orientation update(Acceleration, bool valid, uint64_t now);
};
} // namespace board
