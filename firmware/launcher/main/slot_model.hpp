// Pure launcher presentation logic (no ESP-IDF dependencies; host-tested).
#pragma once
#include <cstddef>

namespace launcher {
enum class SlotState { Empty, Valid, Invalid };
struct Slot {
    const char *label = "";
    bool factory = false;
    SlotState state = SlotState::Empty;
    const char *project_name = "", *version = "";
    const char *expected = ""; // registered project for this slot; "" accepts any image
};
struct Tile {
    char title[24]{}, subtitle[72]{};
    bool launchable = false, highlighted = false;
};
enum class Reason { Normal, Requested, Key, Crash, Fallback };

void title_case(char *dst, size_t cap, const char *label);
// Registered project name of a slot (layout/slots.json), or "" when the slot is unassigned.
const char *expected_project(const char *label);
// Builds tiles for every non-factory slot, in table order. An image whose project differs from
// the slot's registered app (e.g. leftovers from older firmware) is shown but not launchable. Highlights `last` when launchable,
// otherwise the first launchable slot. Returns the number of tiles written.
size_t make_tiles(const Slot *slots, size_t count, const char *last, Tile *tiles, size_t cap);
// Banner explaining an unexpected launcher start; empty for normal or requested starts.
void reason_text(char *dst, size_t cap, Reason, const char *slot_label);
} // namespace launcher
