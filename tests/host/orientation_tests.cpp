#include "board_orientation.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
using namespace board;
int main() {
    const uint16_t src[]{0x1234, 0x5678, 0x9abc, 0xdef0, 0x1357, 0x2468};
    const uint16_t expected_pixels[4][6] = {{0x1234, 0x5678, 0x9abc, 0xdef0, 0x1357, 0x2468},
                                            {0xdef0, 0x1234, 0x1357, 0x5678, 0x2468, 0x9abc},
                                            {0x2468, 0x1357, 0xdef0, 0x9abc, 0x5678, 0x1234},
                                            {0x9abc, 0x2468, 0x5678, 0x1357, 0x1234, 0xdef0}};
    const Rect expected_rects[] = {
        {24, 60, 28, 62}, {418, 24, 420, 28}, {452, 418, 456, 420}, {60, 452, 62, 456}};
    for (unsigned o = 0; o < 4; ++o) {
        uint16_t out[8]{0xbeef, 0, 0, 0, 0, 0, 0, 0xbeef};
        rotate_pixels(src, out + 1, 3, 2, Orientation(o));
        assert(out[0] == 0xbeef && out[7] == 0xbeef);
        assert(!memcmp(out + 1, expected_pixels[o], sizeof src));
        auto r = rotate_rect({24, 60, 28, 62}, Orientation(o));
        auto e = expected_rects[o];
        assert(r.x1 == e.x1 && r.y1 == e.y1 && r.x2 == e.x2 && r.y2 == e.y2);
        for (int x = 0; x < 480; ++x)
            for (int y = 0; y < 480; ++y) {
                auto physical = rotate_point({x, y}, Orientation(o));
                assert(physical.x >= 0 && physical.x < 480 && physical.y >= 0 && physical.y < 480);
                auto logical = unrotate_point(physical, Orientation(o));
                assert(logical.x == x && logical.y == y);
            }
        for (int y = 0; y < 480; y += 12) {
            auto stripe = rotate_rect({0, y, 480, y + 12}, Orientation(o));
            assert(stripe.x1 % 2 == 0 && stripe.y1 % 2 == 0 && stripe.x2 % 2 == 0 &&
                   stripe.y2 % 2 == 0);
            assert((stripe.x2 - stripe.x1) * (stripe.y2 - stripe.y1) == 480 * 12);
        }
    }
    OrientationDetector d;
    uint64_t now = 0;
    auto settle = [&](Acceleration a, Orientation target) {
        auto before = d.current;
        for (int i = 0; i < 6; ++i, now += 50)
            assert(d.update(a, true, now) == before);
        assert(d.update(a, true, now) == target);
        now += 50;
    };
    settle({-1, 0, 0}, Orientation::Clockwise90);
    settle({0, -1, 0}, Orientation::UpsideDown);
    settle({1, 0, 0}, Orientation::Clockwise270);
    settle({0, 1, 0}, Orientation::Upright);
    for (Acceleration invalid : {Acceleration{0, 0, 1},
                                 {0.7f, 0.7f, 0},
                                 {2, 0, 0},
                                 {0.5f, 0, 0},
                                 {0.64f, 0, 0.77f},
                                 {std::numeric_limits<float>::quiet_NaN(), 0, 0}}) {
        for (int i = 0; i < 10; ++i, now += 50)
            assert(d.update(invalid, true, now) == Orientation::Upright);
        assert(!d.pending);
    }
    for (int i = 0; i < 20; ++i, now += 50) {
        assert(d.update(i % 2 ? Acceleration{-0.7f, 0.7f, 0} : Acceleration{-1, 0, 0}, true, now) ==
               Orientation::Upright);
    }
    d.update({-1, 0, 0}, true, now);
    now += 50;
    d.update({-1, 0, 0}, false, now);
    now += 50;
    settle({-1, 0, 0}, Orientation::Clockwise90);
    d.update({0, -1, 0}, true, now);
    now += 5000;
    assert(d.update({0, -1, 0}, true, now) == Orientation::Clockwise90);
    assert(d.since == now); // A long sample gap does not count as settled.
    assert(d.update({0, -1, 0}, true, 0) == Orientation::Clockwise90);
}
