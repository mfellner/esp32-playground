#include "slot_model.hpp"
#include <cassert>
#include <cstring>
using namespace launcher;
int main() {
    char t[24];
    title_case(t, sizeof t, "sparklet");
    assert(!strcmp(t, "Sparklet"));
    title_case(t, sizeof t, "my_app-two");
    assert(!strcmp(t, "My app two"));
    title_case(t, sizeof t, "");
    assert(!strcmp(t, ""));
    char tiny[4];
    title_case(tiny, sizeof tiny, "sparklet");
    assert(!strcmp(tiny, "Spa"));

    Slot slots[4];
    slots[0] = {"launcher", true, SlotState::Valid, "launcher", "0.1.0"};
    slots[1] = {"sparklet", false, SlotState::Valid, "sparkdash", "1.1.0"};
    slots[2] = {"hermes", false, SlotState::Empty, "", ""};
    slots[3] = {"extra", false, SlotState::Invalid, "x", "1"};
    Tile tiles[4];
    // Factory slot is not a tile; empty/invalid slots are visible but not launchable.
    assert(make_tiles(slots, 4, nullptr, tiles, 4) == 3);
    assert(!strcmp(tiles[0].title, "Sparklet") && tiles[0].launchable && tiles[0].highlighted);
    assert(!strcmp(tiles[0].subtitle, "sparkdash 1.1.0"));
    assert(!tiles[1].launchable && !tiles[1].highlighted &&
           !strcmp(tiles[1].subtitle, "Not installed"));
    assert(!tiles[2].launchable && strstr(tiles[2].subtitle, "Damaged"));
    // The last app is highlighted only when it can launch; otherwise the first launchable one.
    assert(make_tiles(slots, 4, "hermes", tiles, 4) == 3 && tiles[0].highlighted);
    slots[2] = {"hermes", false, SlotState::Valid, "hermes", "0.0.1"};
    assert(make_tiles(slots, 4, "hermes", tiles, 4) == 3 && !tiles[0].highlighted &&
           tiles[1].highlighted);
    // Capacity is respected and nothing launchable means nothing highlighted.
    assert(make_tiles(slots, 4, nullptr, tiles, 1) == 1);
    Slot none[1] = {{"sparklet", false, SlotState::Empty, "", ""}};
    assert(make_tiles(none, 1, "sparklet", tiles, 4) == 1 && !tiles[0].highlighted);

    // Leftover images from other firmware are visible but not launchable.
    assert(!strcmp(expected_project("sparklet"), "sparkdash") && !strcmp(expected_project("hermes"), "hermes_gadget"));
    assert(!*expected_project("other"));
    Slot foreign[1] = {{"sparklet", false, SlotState::Valid, "xiaozhi", "2.4.0", "sparkdash"}};
    assert(make_tiles(foreign, 1, "sparklet", tiles, 4) == 1 && !tiles[0].launchable &&
           !tiles[0].highlighted && !strcmp(tiles[0].subtitle, "Unexpected image: xiaozhi"));

    char r[72];
    reason_text(r, sizeof r, Reason::Normal, "sparklet");
    assert(!*r);
    reason_text(r, sizeof r, Reason::Requested, "sparklet");
    assert(!*r);
    reason_text(r, sizeof r, Reason::Crash, "sparklet");
    assert(!strcmp(r, "Sparklet stopped after repeated crashes"));
    reason_text(r, sizeof r, Reason::Fallback, "");
    assert(!strcmp(r, "The app could not start (image invalid)"));
    reason_text(r, sizeof r, Reason::Key, nullptr);
    assert(!strcmp(r, "Opened with KEY at startup"));
}
