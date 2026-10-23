#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "gc8_ble.h"
#include "nav_arrow_assets.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

namespace {
constexpr auto *TAG = "gc8_display";
constexpr uint32_t kBlack = 0x030609;
constexpr uint32_t kWhite = 0xF5F9FF;
constexpr uint32_t kMuted = 0x91A0B3;
constexpr uint32_t kBlue = 0x43A9F7;
constexpr uint32_t kRule = 0x175B99;
constexpr uint32_t kPanel = 0x071320;
constexpr uint32_t kRed = 0xE04D61;
constexpr int kDiameter = 466;

lv_obj_t *connection = nullptr;
lv_obj_t *distance = nullptr;
lv_obj_t *street = nullptr;
lv_obj_t *destination = nullptr;
lv_obj_t *maneuver_graphic = nullptr;
lv_obj_t *afr_value = nullptr;
lv_obj_t *afr_status = nullptr;
lv_obj_t *boost_value = nullptr;
Gc8BleSnapshot previous = {};
bool first_update = true;

lv_obj_t *make_label(lv_obj_t *parent, const char *value, const lv_font_t *font,
                     uint32_t color, int x, int y, int width,
                     lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    auto *obj = lv_label_create(parent);
    lv_label_set_text(obj, value);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_obj_set_width(obj, width);
    lv_obj_set_pos(obj, x, y);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    return obj;
}

// Pre-rendered alpha assets replace LVGL line-based arrows.
void draw_arrow(const char *turn) {
    const lv_image_dsc_t *source = nullptr;
    if (std::strcmp(turn, "RIGHT") == 0) source = &nav_right_image;
    else if (std::strcmp(turn, "LEFT") == 0) source = &nav_left_image;
    else if (std::strcmp(turn, "STRAIGHT") == 0) source = &nav_straight_image;
    lv_image_set_src(maneuver_graphic, source);
    if (source) {
        lv_obj_remove_flag(maneuver_graphic, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(maneuver_graphic, LV_OBJ_FLAG_HIDDEN);
    }
}

void refresh_navigation(lv_timer_t *) {
    const auto current = gc8_ble_snapshot();
    if (!first_update && std::memcmp(&previous, &current, sizeof(current)) == 0) return;
    first_update = false;
    previous = current;

    lv_label_set_text(connection, current.connected ? "PHONE CONNECTED" : "PHONE WAITING");
    if (current.connected && current.has_demo_afr) {
        lv_label_set_text(afr_value, current.demo_afr);
        lv_label_set_text(afr_status, "DEMO ONLY");
    } else {
        lv_label_set_text(afr_value, "--.-");
        lv_label_set_text(afr_status, "NO SENSOR");
    }
    if (current.connected && current.has_navigation) {
        draw_arrow(current.maneuver);
        lv_label_set_text(distance, current.distance);
        lv_label_set_text(street, current.road);
        lv_label_set_text(destination, *current.destination ? current.destination : "Not provided");
    } else {
        draw_arrow("");
        lv_label_set_text(distance, "--");
        lv_label_set_text(street, "Start navigation");
        lv_label_set_text(destination, "--");
    }
    // Navigation line stays inside its own top-right region.
    // Recalculate after each route update because the line count changes.
    lv_obj_update_layout(street);
    const int text_height = lv_obj_get_height(street);
    const int top = 165 - (text_height / 2);
    lv_obj_set_y(street, top < 126 ? 126 : (top > 185 ? 185 : top));
}

void add_line(lv_obj_t *parent, int x1, int y1, int x2, int y2,
              uint32_t color, int width, lv_opa_t opacity = LV_OPA_COVER) {
    auto *line = lv_line_create(parent);
    lv_point_precise_t points[] = {
        {static_cast<lv_value_precise_t>(x1), static_cast<lv_value_precise_t>(y1)},
        {static_cast<lv_value_precise_t>(x2), static_cast<lv_value_precise_t>(y2)}
    };
    lv_line_set_points(line, points, 2);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_opa(line, opacity, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
}

void draw_home() {
    auto *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBlack), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Restrained cockpit header. Battery percentage is intentionally unavailable:
    // this firmware does not yet have a verified battery voltage input.
    make_label(screen, "GC8", &lv_font_montserrat_20,
               kBlue, 183, 18, 100, LV_TEXT_ALIGN_CENTER);
    connection = make_label(screen, "PHONE WAITING", &lv_font_montserrat_16,
                            kMuted, 119, 49, 228, LV_TEXT_ALIGN_CENTER);
    make_label(screen, "BAT --", &lv_font_montserrat_16,
               kMuted, 346, 24, 92, LV_TEXT_ALIGN_RIGHT);

    // Perspective-road motif: intentionally dim behind the main navigation.
    add_line(screen, 226, 197, 78, 267, kBlue, 2, LV_OPA_20);
    add_line(screen, 240, 197, 388, 267, kBlue, 2, LV_OPA_20);
    add_line(screen, 233, 200, 233, 260, kBlue, 2, LV_OPA_20);
    add_line(screen, 233, 265, 233, 273, kBlue, 2, LV_OPA_40);

    maneuver_graphic = lv_image_create(screen);
    lv_obj_set_pos(maneuver_graphic, 59, 101);
    lv_obj_set_size(maneuver_graphic, 132, 140);
    lv_obj_set_style_image_recolor(maneuver_graphic, lv_color_hex(kBlue), 0);
    lv_obj_set_style_image_recolor_opa(maneuver_graphic, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(maneuver_graphic, 0, 0);
    lv_obj_set_style_pad_all(maneuver_graphic, 0, 0);
    lv_obj_remove_flag(maneuver_graphic, LV_OBJ_FLAG_SCROLLABLE);

    distance = make_label(screen, "--", &lv_font_montserrat_26,
                          kWhite, 204, 110, 220, LV_TEXT_ALIGN_LEFT);
    street = make_label(screen, "Start navigation", &lv_font_montserrat_20,
                        kWhite, 204, 153, 220, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(street, LV_LABEL_LONG_WRAP);

    add_line(screen, 42, 273, 424, 273, kBlue, 2, LV_OPA_COVER);

    // Destination gets two bounded lines; never crosses the instrument divider.
    make_label(screen, "DESTINATION", &lv_font_montserrat_16,
               kBlue, 91, 287, 245);
    make_label(screen, ">>", &lv_font_montserrat_20,
               kBlue, 53, 311, 40);
    destination = make_label(screen, "--", &lv_font_montserrat_20,
                             kWhite, 91, 308, 333);
    lv_label_set_long_mode(destination, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(destination, 49);
    add_line(screen, 52, 367, 414, 367, kRule, 1);

    // Two-column instrument cluster. Unconnected sensors never show fake values.
    make_label(screen, "AFR", &lv_font_montserrat_16,
               kBlue, 80, 424, 125, LV_TEXT_ALIGN_CENTER);
    afr_value = make_label(screen, "--.-", &lv_font_montserrat_26,
                           kWhite, 72, 380, 142, LV_TEXT_ALIGN_CENTER);
    afr_status = make_label(screen, "NO SENSOR", &lv_font_montserrat_16,
                            kMuted, 66, 445, 154, LV_TEXT_ALIGN_CENTER);
    add_line(screen, 233, 379, 233, 451, kRule, 1);
    boost_value = make_label(screen, "--.-", &lv_font_montserrat_26,
                             kWhite, 260, 380, 142, LV_TEXT_ALIGN_CENTER);
    make_label(screen, "BOOST", &lv_font_montserrat_16,
               kBlue, 269, 424, 126, LV_TEXT_ALIGN_CENTER);
    make_label(screen, "NO SENSOR", &lv_font_montserrat_16,
               kMuted, 254, 445, 156, LV_TEXT_ALIGN_CENTER);

    lv_timer_create(refresh_navigation, 250, nullptr);
    refresh_navigation(nullptr);
}

} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting GC8 vector navigation dashboard");
    bsp_display_start();
    bsp_display_lock(-1);
    draw_home();
    bsp_display_unlock();
    gc8_ble_start();
}
