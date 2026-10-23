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
constexpr uint32_t kRed = 0xFF526B;
constexpr uint32_t kRule = 0x205D92;

lv_obj_t *connection = nullptr;
lv_obj_t *distance = nullptr;
lv_obj_t *street = nullptr;
lv_obj_t *destination = nullptr;
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

void add_rule(lv_obj_t *screen, int x, int y, int width, uint32_t color, int height) {
    auto *rule = lv_obj_create(screen);
    lv_obj_set_pos(rule, x, y);
    lv_obj_set_size(rule, width, height);
    lv_obj_set_style_bg_color(rule, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 0, 0);
    lv_obj_set_style_pad_all(rule, 0, 0);
    lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(rule, LV_OBJ_FLAG_CLICKABLE);
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

    // The destination is displayed as soon as Start is confirmed, even
    // when Maps has not yet published a positive turn distance.
    const bool has_destination = current.connected && *current.destination;
    lv_label_set_text(destination, has_destination ? current.destination : "--");
    if (current.connected && current.has_navigation) {
        draw_arrow(current.maneuver);
        lv_label_set_text(distance, current.distance);
        lv_label_set_text(street, current.road);
    } else {
        draw_arrow("");
        lv_label_set_text(distance, "--");
        lv_label_set_text(street, has_destination ? "Getting directions..." : "Start navigation");
    }
}

void draw_home() {
    auto *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBlack), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // These pre-rasterized A8 illustrations are rendered once, not redrawn
    // in every 250ms BLE refresh. Layout fits the circular 466px viewport.
    cockpit_art::rasterize();
    add_sprite(screen, &cockpit_art::road, 73, 187, kBlue);
    add_sprite(screen, &cockpit_art::left_arc, 47, 321, kBlue);
    add_sprite(screen, &cockpit_art::right_arc, 323, 321, kRed);

    // Header is deliberately lower than the clipped corners of a circle.
    make_label(screen, "GC8", &lv_font_montserrat_26,
               kBlue, 179, 28, 108, LV_TEXT_ALIGN_CENTER);
    connection = make_label(screen, "BLE WAITING", &lv_font_montserrat_16,
                            kMuted, 121, 65, 224, LV_TEXT_ALIGN_CENTER);
    add_sprite(screen, &cockpit_art::battery_icon, 322, 73, kMuted);
    make_label(screen, "--", &lv_font_montserrat_16, kMuted, 354, 70, 34);
    // No verified battery-voltage input: do not invent percentage or runtime.

    maneuver_graphic = lv_image_create(screen);
    lv_obj_set_pos(maneuver_graphic, 66, 105);
    lv_obj_set_size(maneuver_graphic, 112, 132);
    lv_obj_set_style_image_recolor(maneuver_graphic, lv_color_hex(kBlue), 0);
    lv_obj_set_style_image_recolor_opa(maneuver_graphic, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(maneuver_graphic, 0, 0);
    lv_obj_set_style_pad_all(maneuver_graphic, 0, 0);
    lv_obj_remove_flag(maneuver_graphic, LV_OBJ_FLAG_SCROLLABLE);

    distance = make_label(screen, "--", &lv_font_montserrat_26,
                          kWhite, 205, 117, 188, LV_TEXT_ALIGN_LEFT);
    street = make_label(screen, "Start navigation", &lv_font_montserrat_20,
                        kWhite, 205, 159, 194, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(street, 194, 53);
    lv_label_set_long_mode(street, LV_LABEL_LONG_DOT);

    add_rule(screen, 68, 273, 330, kBlue, 2);

    // A flag image and a height-bounded two-line address prevent any destination
    // from reaching the AFR/boost readouts; LVGL ellipsizes long addresses.
    add_sprite(screen, &cockpit_art::finish_flag, 86, 293, kWhite);
    destination = make_label(screen, "--", &lv_font_montserrat_20,
                             kWhite, 130, 296, 251, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(destination, 251, 52);
    lv_label_set_long_mode(destination, LV_LABEL_LONG_DOT);

    add_rule(screen, 79, 358, 308, kRule, 1);

    // Both numbers and labels stay inside the circular bottom safe area.
    // Boost intentionally remains unavailable until a real sensor is connected.
    afr_value = make_label(screen, "--.-", &lv_font_montserrat_26,
                           kWhite, 122, 373, 103, LV_TEXT_ALIGN_CENTER);
    make_label(screen, "--.-", &lv_font_montserrat_26,
               kWhite, 253, 373, 103, LV_TEXT_ALIGN_CENTER);
    afr_label = make_label(screen, "AFR", &lv_font_montserrat_16,
                           kBlue, 124, 412, 100, LV_TEXT_ALIGN_CENTER);
    make_label(screen, "BOOST", &lv_font_montserrat_16,
               kBlue, 249, 412, 102, LV_TEXT_ALIGN_CENTER);
    add_rule(screen, 233, 376, 1, kRule, 51);

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
