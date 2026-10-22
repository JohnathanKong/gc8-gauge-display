#include <cstring>
#include <cstdio>
#include "esp_log.h"
#include "gc8_ble.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

namespace {
constexpr auto *TAG = "gc8_display";
constexpr uint32_t kBlack = 0x05070A;
constexpr uint32_t kWhite = 0xF5F7FA;
constexpr uint32_t kMuted = 0x8293A5;
constexpr uint32_t kAccent = 0xE7B35D;
constexpr uint32_t kRule = 0x27323E;
constexpr int kScreen = 466;

lv_obj_t *phone_label = nullptr;
lv_obj_t *distance_label = nullptr;
lv_obj_t *road_label = nullptr;
lv_obj_t *nav_hint = nullptr;
lv_obj_t *destination_label = nullptr;
lv_obj_t *arrow = nullptr;
char previous_distance[20] = {};
char previous_road[48] = {};
char previous_maneuver[16] = {};
char previous_destination[48] = {};
bool previous_connected = false;
bool previous_navigation = false;
bool first_refresh = true;

lv_obj_t *label(lv_obj_t *parent, const char *value, const lv_font_t *font,
                uint32_t color, int y, int width = kScreen) {
    auto *obj = lv_label_create(parent);
    lv_label_set_text(obj, value);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(obj, width);
    lv_obj_set_pos(obj, (kScreen - width) / 2, y);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    return obj;
}

void arrow_segment(lv_obj_t *parent, int x1, int y1, int x2, int y2) {
    auto *segment = lv_obj_create(parent);
    const int dx = x2 - x1;
    const int dy = y2 - y1;
    // Horizontal or vertical rectangular strokes keep the arrow crisp without
    // relying on glyph support in the embedded Montserrat font.
    lv_obj_set_pos(segment, dx == 0 ? x1 - 8 : (x1 < x2 ? x1 : x2),
                   dy == 0 ? y1 - 8 : (y1 < y2 ? y1 : y2));
    lv_obj_set_size(segment, dx == 0 ? 16 : (dx < 0 ? -dx : dx) + 16,
                    dy == 0 ? 16 : (dy < 0 ? -dy : dy) + 16);
    lv_obj_set_style_bg_color(segment, lv_color_hex(kWhite), 0);
    lv_obj_set_style_border_width(segment, 0, 0);
    lv_obj_set_style_radius(segment, 8, 0);
    lv_obj_set_style_pad_all(segment, 0, 0);
    lv_obj_remove_flag(segment, LV_OBJ_FLAG_SCROLLABLE);
}

void draw_maneuver(const char *maneuver) {
    lv_obj_clean(arrow);
    if (std::strcmp(maneuver, "RIGHT") == 0) {
        arrow_segment(arrow, 82, 117, 82, 58);
        arrow_segment(arrow, 82, 58, 155, 58);
        // Arrow head: compact rectangular steps, readable at a glance.
        arrow_segment(arrow, 145, 38, 145, 78);
        arrow_segment(arrow, 162, 49, 162, 67);
    } else if (std::strcmp(maneuver, "LEFT") == 0) {
        arrow_segment(arrow, 154, 117, 154, 58);
        arrow_segment(arrow, 82, 58, 154, 58);
        arrow_segment(arrow, 82, 38, 82, 78);
        arrow_segment(arrow, 65, 49, 65, 67);
    } else if (std::strcmp(maneuver, "STRAIGHT") == 0) {
        arrow_segment(arrow, 118, 117, 118, 38);
        arrow_segment(arrow, 101, 48, 101, 68);
        arrow_segment(arrow, 135, 48, 135, 68);
    } else {
        label(arrow, "--", &lv_font_montserrat_26, kMuted, 48, 230);
    }
}

void refresh_ble_ui(lv_timer_t *) {
    const auto state = gc8_ble_snapshot();
    if (!first_refresh && state.connected == previous_connected &&
        state.has_navigation == previous_navigation &&
        std::strcmp(state.distance, previous_distance) == 0 &&
        std::strcmp(state.road, previous_road) == 0 &&
        std::strcmp(state.maneuver, previous_maneuver) == 0 &&
        std::strcmp(state.destination, previous_destination) == 0) return;
    first_refresh = false;
    previous_connected = state.connected;
    previous_navigation = state.has_navigation;
    std::snprintf(previous_distance, sizeof(previous_distance), "%s", state.distance);
    std::snprintf(previous_road, sizeof(previous_road), "%s", state.road);
    std::snprintf(previous_maneuver, sizeof(previous_maneuver), "%s", state.maneuver);
    std::snprintf(previous_destination, sizeof(previous_destination), "%s", state.destination);

    lv_label_set_text(phone_label, state.connected ? "PHONE CONNECTED" : "PHONE WAITING");
    if (state.connected && state.has_navigation) {
        draw_maneuver(state.maneuver);
        lv_label_set_text(distance_label, state.distance);
        lv_label_set_text(road_label, state.road);
        lv_label_set_text(nav_hint, "NEXT TURN");
        lv_label_set_text_fmt(destination_label, "TO: %s", *state.destination ? state.destination : "Not provided");
    } else {
        draw_maneuver("");
        lv_label_set_text(distance_label, "--");
        lv_label_set_text(road_label, "Start navigation");
        lv_label_set_text(nav_hint, "NO ROUTE DATA");
        lv_label_set_text(destination_label, "TO: --");
    }
}

void render_navigation() {
    auto *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBlack), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    phone_label = label(screen, "PHONE WAITING", &lv_font_montserrat_16, kMuted, 30);
    arrow = lv_obj_create(screen);
    lv_obj_set_pos(arrow, 115, 66);
    lv_obj_set_size(arrow, 236, 138);
    lv_obj_set_style_bg_opa(arrow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arrow, 0, 0);
    lv_obj_set_style_pad_all(arrow, 0, 0);
    lv_obj_remove_flag(arrow, LV_OBJ_FLAG_SCROLLABLE);

    nav_hint = label(screen, "NO ROUTE DATA", &lv_font_montserrat_16, kMuted, 206);
    distance_label = label(screen, "--", &lv_font_montserrat_26, kWhite, 233);
    road_label = label(screen, "Start navigation", &lv_font_montserrat_16, kWhite, 267, 356);
    destination_label = label(screen, "TO: --", &lv_font_montserrat_16, kMuted, 292, 330);

    auto *divider = lv_obj_create(screen);
    lv_obj_set_pos(divider, 105, 324);
    lv_obj_set_size(divider, 256, 2);
    lv_obj_set_style_bg_color(divider, lv_color_hex(kRule), 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);

    label(screen, "WIDEBAND AFR", &lv_font_montserrat_16, kMuted, 342);
    label(screen, "--.-", &lv_font_montserrat_26, kWhite, 367);
    // Decorative neutral arc is deliberately omitted until calibrated AFR
    // thresholds and a real wideband signal exist.
    label(screen, "NO SENSOR", &lv_font_montserrat_16, kAccent, 408);
    lv_timer_create(refresh_ble_ui, 250, nullptr);
    refresh_ble_ui(nullptr);
}
} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting GC8 navigation-first display");
    bsp_display_start();
    bsp_display_lock(-1);
    render_navigation();
    bsp_display_unlock();
    gc8_ble_start();
}
