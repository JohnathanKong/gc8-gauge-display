#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "gc8_ble.h"
#include "nav_arrow_assets.h"
#include "cockpit_art.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

namespace {
constexpr auto *TAG = "gc8_display";
constexpr uint32_t kBlack = 0x030609;
constexpr uint32_t kWhite = 0xF5F9FF;
constexpr uint32_t kMuted = 0x8297AE;
constexpr uint32_t kBlue = 0x229EFF;

lv_obj_t *connection = nullptr;
lv_obj_t *distance = nullptr;
lv_obj_t *street = nullptr;
lv_obj_t *maneuver_graphic = nullptr;
lv_obj_t *afr_value = nullptr;
lv_obj_t *afr_label = nullptr;
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

void add_sprite(lv_obj_t *screen, const lv_image_dsc_t *source,
                int x, int y, uint32_t color) {
    auto *sprite = lv_image_create(screen);
    lv_image_set_src(sprite, source);
    lv_obj_set_pos(sprite, x, y);
    lv_obj_set_style_image_recolor(sprite, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(sprite, LV_OPA_COVER, 0);
    lv_obj_remove_flag(sprite, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(sprite, LV_OBJ_FLAG_CLICKABLE);
}

// Preserve the previously tested LEFT/RIGHT/STRAIGHT artwork and BLE protocol.
void draw_arrow(const char *turn) {
    const lv_image_dsc_t *source = nullptr;
    if (std::strcmp(turn, "RIGHT") == 0) source = &nav_right_image;
    else if (std::strcmp(turn, "LEFT") == 0) source = &nav_left_image;
    else if (std::strcmp(turn, "STRAIGHT") == 0) source = &nav_straight_image;
    if (source) {
        lv_image_set_src(maneuver_graphic, source);
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

    lv_label_set_text(connection, current.connected ? "BLE CONNECTED" : "BLE WAITING");
    if (current.connected && current.has_demo_afr) {
        lv_label_set_text(afr_value, current.demo_afr);
        lv_label_set_text(afr_label, "AFR DEMO");
    } else {
        lv_label_set_text(afr_value, "--.-");
        lv_label_set_text(afr_label, "AFR");
    }

    // Driving view deliberately omits destination: a postal address is too
    // verbose to read safely from a small gauge mounted far from the driver.
    // BLE still accepts/stores destination for a future details page.
    if (current.connected && current.has_navigation) {
        draw_arrow(current.maneuver);
        lv_label_set_text(distance, current.distance);
        lv_label_set_text(street, current.road);
    } else {
        draw_arrow("");
        lv_label_set_text(distance, "--");
        // START comes from a Maps navigation notification, not
        // Accessibility or a destination string. Until a real positive
        // maneuver distance arrives, do not invent turn guidance.
        const bool route_started = current.connected && current.route_active;
        lv_label_set_text(street, route_started ? "Getting directions..." :
                          current.connected ? "Start navigation" : "Connect phone");
    }
}

void draw_home() {
    auto *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBlack), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Readable from the driver's seat: 2-column navigation above a single,
    // centered AFR readout. All visible elements respect the 466px round edge.
    // No destination, boost, decorative road or split gauge divider.
    cockpit_art::rasterize();
    add_sprite(screen, &cockpit_art::afr_cradle, 93, 321, kBlue);

    make_label(screen, "GC8", &lv_font_montserrat_26,
               kBlue, 179, 23, 108, LV_TEXT_ALIGN_CENTER);
    connection = make_label(screen, "BLE WAITING", &lv_font_montserrat_20,
                            kMuted, 119, 64, 228, LV_TEXT_ALIGN_CENTER);

    // Retain exactly the 112x132 arrow artwork the owner liked.
    // Left column: arrow above a large, centered next-turn distance.
    maneuver_graphic = lv_image_create(screen);
    lv_obj_set_pos(maneuver_graphic, 83, 106);
    lv_obj_set_size(maneuver_graphic, 112, 132);
    lv_obj_set_style_image_recolor(maneuver_graphic, lv_color_hex(kBlue), 0);
    lv_obj_set_style_image_recolor_opa(maneuver_graphic, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(maneuver_graphic, 0, 0);
    lv_obj_set_style_pad_all(maneuver_graphic, 0, 0);
    lv_obj_remove_flag(maneuver_graphic, LV_OBJ_FLAG_SCROLLABLE);

    distance = make_label(screen, "--", &lv_font_montserrat_40,
                          kWhite, 43, 247, 196, LV_TEXT_ALIGN_CENTER);

    // Right column: use the available height for large multi-line road text.
    // LVGL ellipsizes only genuinely too-long road names. No overlap with AFR.
    street = make_label(screen, "Start navigation", &lv_font_montserrat_32,
                        kWhite, 233, 141, 172, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(street, 172, 149);
    lv_label_set_long_mode(street, LV_LABEL_LONG_DOT);

    // AFR sits ABOVE a clean, nonfunctional lower semicircle cradle. This is
    // just a static outline, not an invented AFR safety/lean/rich scale.
    afr_label = make_label(screen, "AFR", &lv_font_montserrat_20,
                           kBlue, 158, 324, 150, LV_TEXT_ALIGN_CENTER);
    afr_value = make_label(screen, "--.-", &lv_font_montserrat_48,
                           kWhite, 101, 349, 264, LV_TEXT_ALIGN_CENTER);

    lv_timer_create(refresh_navigation, 250, nullptr);
    refresh_navigation(nullptr);
}
} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting GC8 cockpit navigation dashboard");
    bsp_display_start();
    bsp_display_lock(-1);
    draw_home();
    bsp_display_unlock();
    gc8_ble_start();
}
