#include "board_orientation.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace board {
Point rotate_point(Point p, Orientation o, int side) {
    switch (o) {
    case Orientation::Clockwise90:
        return {side - 1 - p.y, p.x};
    case Orientation::UpsideDown:
        return {side - 1 - p.x, side - 1 - p.y};
    case Orientation::Clockwise270:
        return {p.y, side - 1 - p.x};
    default:
        return p;
    }
}
Point unrotate_point(Point p, Orientation o, int side) {
    return rotate_point(p, Orientation((4 - unsigned(o)) % 4), side);
}
Rect rotate_rect(Rect r, Orientation o, int side) {
    auto a = rotate_point({r.x1, r.y1}, o, side);
    auto b = rotate_point({r.x2 - 1, r.y2 - 1}, o, side);
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x) + 1, std::max(a.y, b.y) + 1};
}
void rotate_pixels(const uint16_t *src, uint16_t *dst, int w, int h, Orientation o) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            size_t i;
            switch (o) {
            case Orientation::Clockwise90:
                i = x * h + (h - 1 - y);
                break;
            case Orientation::UpsideDown:
                i = (h - 1 - y) * w + (w - 1 - x);
                break;
            case Orientation::Clockwise270:
                i = (w - 1 - x) * h + y;
                break;
            default:
                i = y * w + x;
                break;
            }
            dst[i] = src[y * w + x];
        }
}
Orientation OrientationDetector::update(Acceleration a, bool valid, uint64_t now) {
    const float mag2 = a.x * a.x + a.y * a.y + a.z * a.z;
    const float x = std::fabs(a.x), y = std::fabs(a.y);
    if (now < last_sample || now - last_sample > 150)
        pending = false;
    last_sample = now;
    if (!valid || !std::isfinite(mag2) || mag2 < 0.75f * 0.75f || mag2 > 1.25f * 1.25f ||
        std::max(x, y) < 0.65f || std::fabs(x - y) < 0.2f) {
        pending = false;
        return current;
    }
    const auto next = x > y ? (a.x > 0 ? Orientation::Clockwise270 : Orientation::Clockwise90)
                            : (a.y > 0 ? Orientation::Upright : Orientation::UpsideDown);
    if (next == current)
        pending = false;
    else if (!pending || candidate != next) {
        candidate = next;
        since = now;
        pending = true;
    } else if (now - since >= 300) {
        current = next;
        pending = false;
    }
    return current;
}
} // namespace board
