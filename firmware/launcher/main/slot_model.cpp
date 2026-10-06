#include "slot_model.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>

namespace launcher {
void title_case(char *dst, size_t cap, const char *label) {
    if (!cap)
        return;
    snprintf(dst, cap, "%s", label ? label : "");
    for (char *c = dst; *c; ++c)
        if (*c == '_' || *c == '-')
            *c = ' ';
    if (*dst)
        *dst = char(toupper(static_cast<unsigned char>(*dst)));
}

const char *expected_project(const char *label) {
    // Mirrors components/app_switch/layout/slots.json.
    if (!label) return "";
    if (!strcmp(label, "sparklet")) return "sparkdash";
    if (!strcmp(label, "hermes")) return "hermes_gadget";
    return "";
}

size_t make_tiles(const Slot *slots, size_t count, const char *last, Tile *tiles, size_t cap) {
    size_t n = 0, first = cap, chosen = cap;
    for (size_t i = 0; i < count && n < cap; ++i) {
        const Slot &s = slots[i];
        if (s.factory)
            continue;
        Tile &t = tiles[n] = Tile{};
        title_case(t.title, sizeof t.title, s.label);
        switch (s.state) {
        case SlotState::Valid:
            if (*s.expected && strcmp(s.expected, s.project_name)) {
                snprintf(t.subtitle, sizeof t.subtitle, "Unexpected image: %s", s.project_name);
                break;
            }
            t.launchable = true;
            snprintf(t.subtitle, sizeof t.subtitle, "%s %s", s.project_name, s.version);
            if (first == cap)
                first = n;
            if (last && !strcmp(last, s.label))
                chosen = n;
            break;
        case SlotState::Invalid:
            snprintf(t.subtitle, sizeof t.subtitle, "Damaged image - reinstall over USB");
            break;
        case SlotState::Empty:
            snprintf(t.subtitle, sizeof t.subtitle, "Not installed");
            break;
        }
        ++n;
    }
    if (chosen == cap)
        chosen = first;
    if (chosen != cap)
        tiles[chosen].highlighted = true;
    return n;
}

void reason_text(char *dst, size_t cap, Reason reason, const char *slot_label) {
    char title[24];
    title_case(title, sizeof title, slot_label && *slot_label ? slot_label : "the app");
    switch (reason) {
    case Reason::Key:
        snprintf(dst, cap, "Opened with KEY at startup");
        break;
    case Reason::Crash:
        snprintf(dst, cap, "%s stopped after repeated crashes", title);
        break;
    case Reason::Fallback:
        snprintf(dst, cap, "%s could not start (image invalid)", title);
        break;
    default:
        if (cap)
            *dst = 0;
        break;
    }
}
} // namespace launcher
