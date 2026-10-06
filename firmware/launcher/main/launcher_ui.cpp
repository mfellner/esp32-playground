#include "board.hpp"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "launcher.hpp"
#include "slot_model.hpp"
#include <cstdio>
#include <cstring>

namespace launcher {
namespace {
enum class Page { Home, Device, Buttons, Reset };
enum Action : intptr_t { ActionDevice = 1, ActionBack, ActionButtons, ActionReset, ActionErase };
constexpr uint32_t Bg = 0x101110, TileBg = 0x232523, Text = 0xf1f1ed, Muted = 0xaaa9a4,
                   Amber = 0xe8ac2b, Red = 0xf05b51, Green = 0x4ac09a;
constexpr uint64_t DimAfterMs = 60 * 1000;
Page page = Page::Home;
Tile tiles[MaxSlots];
size_t tile_slot[MaxSlots]; // tile index -> state.slots index
size_t tile_count = 0;
lv_obj_t *banner, *probe_label, *slider;
char launching[17]{};
uint64_t launch_at = 0, last_touch = 0;
bool asleep = false, dimmed = false, was_down = false, consume = false;
unsigned shown_brightness = 0;

uint64_t now_ms() {
    return uint64_t(esp_timer_get_time() / 1000);
}
lv_color_t color(uint32_t c) {
    return lv_color_hex(c);
}
lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, const lv_font_t *font, uint32_t c = Text) {
    auto *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color(c), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, "");
    return l;
}
void build();
void show(Page next) {
    page = next;
    build();
}
void apply_brightness() {
    unsigned b = asleep ? 0 : dimmed ? 10 : state.brightness;
    if (b != shown_brightness) {
        board::brightness(b);
        shown_brightness = b;
    }
}
void set_banner(const char *text, uint32_t c) {
    if (!banner)
        return;
    lv_label_set_text(banner, text);
    lv_obj_set_style_text_color(banner, color(c), 0);
}
void launch(size_t slot) {
    const auto &s = state.slots[slot];
    if (s.state != APP_SWITCH_SLOT_VALID || launch_at)
        return;
    char title[24], text[48];
    title_case(title, sizeof title, s.label);
    snprintf(text, sizeof text, "Starting %s...", title);
    set_banner(text, Green);
    snprintf(launching, sizeof launching, "%s", s.label);
    launch_at = now_ms() + 150; // let the banner reach the panel first
}
void on_action(lv_event_t *e) {
    switch (intptr_t(lv_event_get_user_data(e))) {
    case ActionDevice:
        show(Page::Device);
        break;
    case ActionBack:
        show(Page::Home);
        break;
    case ActionButtons:
        show(Page::Buttons);
        break;
    case ActionReset:
        show(Page::Reset);
        break;
    case ActionErase:
        storage_erase_all();
        app_switch_open_launcher(); // restart with empty settings
        break;
    }
}
void on_tile(lv_event_t *e) {
    launch(tile_slot[uintptr_t(lv_event_get_user_data(e))]);
}
void on_slider(lv_event_t *) {
    state.brightness = unsigned(lv_slider_get_value(slider));
    apply_brightness();
}
void on_slider_released(lv_event_t *) {
    storage_save_brightness(state.brightness);
}
lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, Action action,
                 uint32_t c = 0x282a27) {
    auto *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, 44);
    lv_obj_set_style_bg_color(b, color(c), 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, on_action, LV_EVENT_CLICKED, reinterpret_cast<void *>(action));
    auto *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, color(Text), 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}
void build_home(lv_obj_t *screen) {
    auto *t = label(screen, 24, 22, 280, &lv_font_montserrat_32);
    lv_label_set_text(t, "Apps");
    button(screen, "Device", 336, 16, 120, ActionDevice);
    banner = label(screen, 24, 70, 432, &lv_font_montserrat_16, Amber);
    char text[72];
    const char *slot_label = app_switch_label(state.reason.slot);
    reason_text(text, sizeof text,
                state.reason.kind == APP_SWITCH_LAUNCH_KEY        ? Reason::Key
                : state.reason.kind == APP_SWITCH_LAUNCH_CRASH    ? Reason::Crash
                : state.reason.kind == APP_SWITCH_LAUNCH_FALLBACK ? Reason::Fallback
                                                                  : Reason::Normal,
                slot_label ? slot_label : "");
    set_banner(text, state.reason.kind == APP_SWITCH_LAUNCH_KEY ? Amber : Red);

    Slot slots[MaxSlots];
    for (size_t i = 0; i < state.count; ++i) {
        const auto &s = state.slots[i];
        slots[i].label = s.label;
        slots[i].factory = s.ota_index == 0xff;
        slots[i].state = s.state == APP_SWITCH_SLOT_VALID     ? SlotState::Valid
                         : s.state == APP_SWITCH_SLOT_INVALID ? SlotState::Invalid
                                                              : SlotState::Empty;
        slots[i].project_name = s.project_name;
        slots[i].version = s.version;
        slots[i].expected = expected_project(s.label);
    }
    tile_count = make_tiles(slots, state.count, state.last, tiles, MaxSlots);
    for (size_t i = 0, t = 0; i < state.count && t < tile_count; ++i)
        if (!slots[i].factory)
            tile_slot[t++] = i;

    auto *list = lv_obj_create(screen);
    lv_obj_set_pos(list, 24, 100);
    lv_obj_set_size(list, 432, 330);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    for (size_t i = 0; i < tile_count; ++i) {
        auto *b = lv_button_create(list);
        lv_obj_set_pos(b, 0, int(i) * 136);
        lv_obj_set_size(b, 432, 120);
        lv_obj_set_style_bg_color(b, color(TileBg), 0);
        lv_obj_set_style_radius(b, 16, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_border_color(b, color(Amber), 0);
        lv_obj_set_style_border_width(b, tiles[i].highlighted ? 3 : 0, 0);
        if (!tiles[i].launchable)
            lv_obj_set_style_bg_opa(b, LV_OPA_40, 0);
        lv_obj_add_event_cb(b, on_tile, LV_EVENT_CLICKED, reinterpret_cast<void *>(i));
        auto *name = label(b, 8, 10, 392, &lv_font_montserrat_32,
                           tiles[i].launchable ? Text : Muted);
        lv_label_set_text(name, tiles[i].title);
        auto *sub = label(b, 8, 58, 392, &lv_font_montserrat_20,
                          state.slots[tile_slot[i]].state == APP_SWITCH_SLOT_INVALID ? Red : Muted);
        lv_label_set_text(sub, tiles[i].subtitle);
    }
    auto *hint = label(screen, 24, 446, 432, &lv_font_montserrat_16, Muted);
    lv_label_set_text(hint, "KEY: open last app   PWR: screen off/on");
}
void build_device(lv_obj_t *screen) {
    auto *t = label(screen, 24, 22, 280, &lv_font_montserrat_32);
    lv_label_set_text(t, "Device");
    button(screen, "Back", 336, 16, 120, ActionBack);
    char info[600];
    size_t used = snprintf(info, sizeof info,
                           "Launcher %s, ESP-IDF %s\nHeap %u B free, %u B minimum\nReset %d, "
                           "start: %s\n",
                           esp_app_get_description()->version, esp_get_idf_version(),
                           unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                           unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                           esp_reset_reason(), reason_name(state.reason.kind));
    for (size_t i = 0; i < state.count && used < sizeof info; ++i) {
        const auto &s = state.slots[i];
        used += snprintf(info + used, sizeof info - used, "%-9s 0x%06lx %4lu KiB %s%s\n", s.label,
                         (unsigned long)s.offset, (unsigned long)(s.size / 1024),
                         s.state == APP_SWITCH_SLOT_VALID     ? s.version
                         : s.state == APP_SWITCH_SLOT_INVALID ? "invalid"
                                                              : "empty",
                         s.boot ? " (boot)" : "");
    }
    auto *l = label(screen, 24, 76, 432, &lv_font_montserrat_16, Muted);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, info);
    auto *b = label(screen, 24, 300, 432, &lv_font_montserrat_16);
    lv_label_set_text(b, "Launcher brightness");
    slider = lv_slider_create(screen);
    lv_obj_set_pos(slider, 40, 330);
    lv_obj_set_size(slider, 400, 14);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, int32_t(state.brightness), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, on_slider, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(slider, on_slider_released, LV_EVENT_RELEASED, nullptr);
    button(screen, "Buttons", 24, 380, 200, ActionButtons);
    button(screen, "Reset settings", 236, 380, 220, ActionReset);
}
void build_buttons(lv_obj_t *screen) {
    auto *t = label(screen, 24, 22, 280, &lv_font_montserrat_32);
    lv_label_set_text(t, "Buttons");
    button(screen, "Back", 336, 16, 120, ActionBack);
    probe_label = label(screen, 24, 84, 432, &lv_font_montserrat_20);
    lv_label_set_long_mode(probe_label, LV_LABEL_LONG_WRAP);
    auto *hint = label(screen, 24, 360, 432, &lv_font_montserrat_16, Muted);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hint, "Press each button. GPIO levels: 1 = released for BOOT/KEY; "
                            "GPIO18 follows PWR (1 = pressed). Holding PWR for 6 s powers off.");
}
void build_reset(lv_obj_t *screen) {
    auto *t = label(screen, 24, 22, 280, &lv_font_montserrat_32);
    lv_label_set_text(t, "Reset");
    button(screen, "Back", 336, 16, 120, ActionBack);
    auto *l = label(screen, 24, 100, 432, &lv_font_montserrat_20);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, "Erase the saved settings of every app on this device, including "
                         "Wi-Fi and server configuration? Installed apps stay.");
    button(screen, "Erase all settings", 24, 300, 432, ActionErase, 0x6b2420);
}
void build() {
    auto *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, color(Bg), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    banner = probe_label = slider = nullptr;
    switch (page) {
    case Page::Home:
        build_home(screen);
        break;
    case Page::Device:
        build_device(screen);
        break;
    case Page::Buttons:
        build_buttons(screen);
        break;
    case Page::Reset:
        build_reset(screen);
        break;
    }
}
bool touch_filter(bool down, int, int) {
    if (down && !was_down) {
        consume = asleep || dimmed; // the first touch only wakes the screen
        asleep = dimmed = false;
        apply_brightness();
    }
    if (down)
        last_touch = now_ms();
    bool consumed = consume;
    if (!down)
        consume = false;
    was_down = down;
    return consumed;
}
void tick(lv_timer_t *) {
    const uint64_t now = now_ms();
    unsigned keys = board::poll_power_key();
    if (keys & board::PowerKeyShort) {
        power_short++;
        asleep = !asleep;
        dimmed = false;
        last_touch = now;
    }
    if (keys & board::PowerKeyLong)
        power_long++;
    if (key_pending.exchange(false) && page == Page::Home) {
        for (size_t i = 0; i < tile_count; ++i)
            if (tiles[i].highlighted)
                launch(tile_slot[i]);
    }
    if (!asleep && !dimmed && now - last_touch > DimAfterMs)
        dimmed = true;
    apply_brightness();
    if (probe_label) {
        char text[200];
        snprintf(text, sizeof text,
                 "BOOT  GPIO9  = %d\nKEY   GPIO10 = %d\nPWR   GPIO18 = %d\n\nKEY presses %u\n"
                 "BOOT holds %u\nPWR short %u, long %u",
                 gpio_get_level(GPIO_NUM_9), gpio_get_level(GPIO_NUM_10),
                 gpio_get_level(GPIO_NUM_18), key_presses.load(), boot_holds.load(),
                 power_short.load(), power_long.load());
        if (strcmp(lv_label_get_text(probe_label), text))
            lv_label_set_text(probe_label, text);
    }
    if (launch_at && now >= launch_at) {
        launch_at = 0;
        storage_save_last(launching);
        esp_err_t err = app_switch_boot(launching); // restarts on success
        char text[64];
        snprintf(text, sizeof text, "Could not start: %s", esp_err_to_name(err));
        set_banner(text, Red);
    }
}
} // namespace

void ui_start() {
    if (!board::lock())
        return;
    last_touch = now_ms();
    build();
    board::set_touch_filter(touch_filter);
    lv_timer_create(tick, 50, nullptr);
    apply_brightness();
    board::unlock();
}
} // namespace launcher
