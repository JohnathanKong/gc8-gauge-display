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
constexpr uint32_t kRule = 0x263644;
constexpr int kDiameter = 466;

lv_obj_t *connection = nullptr;
lv_obj_t *distance = nullptr;
lv_obj_t *street = nullptr;
lv_obj_t *destination = nullptr;
lv_obj_t *maneuver_graphic = nullptr;
lv_obj_t *afr_value = nullptr;
lv_obj_t *afr_status = nullptr;
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
    // Center the wrapped street block vertically, never horizontally.
    // Recalculate after each route update because the line count changes.
    lv_obj_update_layout(street);
    const int text_height = lv_obj_get_height(street);
    const int top = 166 - (text_height / 2);
    lv_obj_set_y(street, top < 108 ? 108 : (top > 205 ? 205 : top));
}

void draw_home() {
    auto *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBlack), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Top edge remains reserved for a future pull-down notification shade.
    make_label(screen, "GC8", &lv_font_montserrat_16, kBlue, 177, 26, 112, LV_TEXT_ALIGN_CENTER);
    connection = make_label(screen, "PHONE WAITING", &lv_font_montserrat_16,
                            kMuted, 126, 51, 214, LV_TEXT_ALIGN_CENTER);

    maneuver_graphic = lv_image_create(screen);
    lv_obj_set_pos(maneuver_graphic, 71, 99);
    lv_obj_set_size(maneuver_graphic, 112, 132);
    lv_obj_set_style_image_recolor(maneuver_graphic, lv_color_hex(kWhite), 0);
    lv_obj_set_style_image_recolor_opa(maneuver_graphic, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(maneuver_graphic, 0, 0);
    lv_obj_set_style_pad_all(maneuver_graphic, 0, 0);
    lv_obj_remove_flag(maneuver_graphic, LV_OBJ_FLAG_SCROLLABLE);

    // Distance sits directly below the maneuver arrow.
    distance = make_label(screen, "--", &lv_font_montserrat_26,
                          kWhite, 54, 240, 184, LV_TEXT_ALIGN_CENTER);
    // A dedicated two-line street box prevents long road names from colliding
    // with distance or the destination section.
    street = make_label(screen, "Start navigation", &lv_font_montserrat_20,
                        kWhite, 231, 137, 185, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(street, LV_LABEL_LONG_WRAP);
    // Natural label height is needed for accurate vertical centering.
    make_label(screen, "Destination:", &lv_font_montserrat_16,
               kMuted, 222, 216, 205);
    destination = make_label(screen, "--", &lv_font_montserrat_20,
                             kWhite, 222, 240, 205);

    auto *divider = lv_obj_create(screen);
    lv_obj_set_pos(divider, 79, 318);
    lv_obj_set_size(divider, 308, 2);
    lv_obj_set_style_bg_color(divider, lv_color_hex(kRule), 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

    // No LVGL arc: its default knob caused a bright dot and its placement
    // obscured the status. Keep the AFR section clean until calibrated data.
    make_label(screen, "WIDEBAND AFR", &lv_font_montserrat_16,
               kMuted, 138, 331, 190, LV_TEXT_ALIGN_CENTER);
    afr_value = make_label(screen, "--.-", &lv_font_montserrat_26,
               kWhite, 155, 359, 156, LV_TEXT_ALIGN_CENTER);
    afr_status = make_label(screen, "NO SENSOR", &lv_font_montserrat_16,
               kBlue, 150, 401, 166, LV_TEXT_ALIGN_CENTER);

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
