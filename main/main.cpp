#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"
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
constexpr uint32_t kRule = 0x205D92;

lv_obj_t *connection = nullptr;
lv_obj_t *clock_label = nullptr;
char displayed_time[20] = "";
lv_obj_t *distance = nullptr;
lv_obj_t *street = nullptr;
lv_obj_t *maneuver_graphic = nullptr;
lv_obj_t *afr_value = nullptr;
lv_obj_t *battery_percent = nullptr;
lv_obj_t *battery_fill = nullptr;
lv_obj_t *battery_outline = nullptr;
lv_obj_t *afr_sweep_sprite = nullptr;
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

lv_obj_t *add_sprite(lv_obj_t *screen, const lv_image_dsc_t *source,
                     int x, int y, uint32_t color,
                     lv_opa_t opacity = LV_OPA_COVER) {
    auto *sprite = lv_image_create(screen);
    lv_image_set_src(sprite, source);
    lv_obj_set_pos(sprite, x, y);
    lv_obj_set_style_image_recolor(sprite, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(sprite, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(sprite, opacity, 0);
    lv_obj_remove_flag(sprite, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(sprite, LV_OBJ_FLAG_CLICKABLE);
    return sprite;
}

// Deliberately separate dividers: both header rules, the vertical
// navigation separator, and the strong horizontal navigation/AFR boundary.
void add_rule(lv_obj_t *screen, int x, int y, int width, int height,
              uint32_t color = kRule, lv_opa_t opacity = LV_OPA_COVER) {
    auto *rule = lv_obj_create(screen);
    lv_obj_set_pos(rule, x, y);
    lv_obj_set_size(rule, width, height);
    lv_obj_set_style_bg_color(rule, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(rule, opacity, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 0, 0);
    lv_obj_set_style_pad_all(rule, 0, 0);
    lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(rule, LV_OBJ_FLAG_CLICKABLE);
}

// Two thin concentric outlines give the circular screen the instrument
// bezel in the reference, with no interactive LVGL arc knob.
void add_rim(lv_obj_t *screen, int inset, uint32_t color, lv_opa_t opacity) {
    auto *ring = lv_obj_create(screen);
    lv_obj_set_pos(ring, inset, inset);
    lv_obj_set_size(ring, 466 - inset * 2, 466 - inset * 2);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(ring, opacity, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
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

    // The clock advances independently of navigation packets. A phone sync
    // sets local wall time and 12/24h preference; monotonic ESP32 uptime
    // supplies the seconds until the next minute/reconnect synchronization.
    char time_text[20] = "--:--";
    if (current.has_clock) {
        const int64_t elapsed_us = esp_timer_get_time() - current.clock_synced_us;
        const int64_t elapsed_seconds = elapsed_us > 0 ? elapsed_us / 1000000 : 0;
        const int second_of_day =
            static_cast<int>((current.clock_seconds + elapsed_seconds) % 86400);
        const int hour = second_of_day / 3600;
        const int minute = (second_of_day / 60) % 60;
        if (current.clock_24h) {
            std::snprintf(time_text, sizeof(time_text), "%02d:%02d", hour, minute);
        } else {
            const int h12 = hour % 12 == 0 ? 12 : hour % 12;
            std::snprintf(time_text, sizeof(time_text), "%d:%02d %s",
                          h12, minute, hour < 12 ? "AM" : "PM");
        }
    }
    if (std::strcmp(displayed_time, time_text) != 0) {
        lv_label_set_text(clock_label, time_text);
        std::snprintf(displayed_time, sizeof(displayed_time), "%s", time_text);
    }
    if (!first_update && std::memcmp(&previous, &current, sizeof(current)) == 0) return;
    first_update = false;
    previous = current;

    lv_label_set_text(connection, current.connected ? "BLE CONNECTED" : "BLE WAITING");
    // Battery refers to the connected PHONE, not an uninstrumented vehicle
    // battery. Hide the icon/value when no verified Android reading exists.
    const bool battery_known = current.connected && current.has_phone_battery;
    if (battery_known) {
        char pct[8];
        std::snprintf(pct, sizeof(pct), "%u%%",
                      static_cast<unsigned>(current.phone_battery_percent));
        lv_label_set_text(battery_percent, pct);
        lv_obj_set_width(battery_fill, current.phone_battery_percent * 17 / 100);
        lv_obj_remove_flag(battery_percent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(battery_outline, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(battery_fill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(battery_percent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_outline, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_fill, LV_OBJ_FLAG_HIDDEN);
    }
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
        // Use 32px for common short roads; 26px for long compound names.
        // This is still large enough for a quick glance but gives longer
        // roads more real estate than the previous narrow right column.
        lv_obj_set_style_text_font(street,
            std::strlen(current.road) > 27 ? &lv_font_montserrat_26 :
                                            &lv_font_montserrat_32, 0);
        lv_label_set_text(street, current.road);
    } else {
        draw_arrow("");
        lv_label_set_text(distance, "--");
        lv_obj_set_style_text_font(street, &lv_font_montserrat_26, 0);
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

    // Reference-matched 466px round cockpit. Layers: atmospheric road,
    // segmented rim gauge, fine dividers, then white text and blue arrow.
    cockpit_art::rasterize();
    add_rim(screen, 5, 0x49667B, LV_OPA_70);
    add_rim(screen, 11, 0x1D3856, LV_OPA_55);
    if (cockpit_art::road.data) {
        add_sprite(screen, &cockpit_art::road,
                   cockpit_art::road_x, cockpit_art::road_y, kBlue,
                   LV_OPA_90);
    }
    if (cockpit_art::afr_sweep.data) {
        afr_sweep_sprite = add_sprite(screen, &cockpit_art::afr_sweep,
                                     cockpit_art::sweep_x,
                                     cockpit_art::sweep_y, kBlue);
    }

    // All reference separators are intentional: upper-left and upper-right
    // rule fragments, vertical arrow/street separator, and AFR horizon line.
    add_rule(screen, 48, 79, 93, 1, 0x2A5474, LV_OPA_65);
    add_rule(screen, 309, 79, 31, 1, 0x2A5474, LV_OPA_65);
    add_rule(screen, 194, 120, 2, 140, kBlue, LV_OPA_55);
    add_rule(screen, 42, 310, 382, 2, kBlue, LV_OPA_80);

    // Phone-clock, not an invented ESP32 date: "--:--" until first sync.
    clock_label = make_label(screen, "--:--", &lv_font_montserrat_40,
                             kBlue, 118, 26, 230, LV_TEXT_ALIGN_CENTER);
    connection = make_label(screen, "BLE WAITING", &lv_font_montserrat_16,
                            kMuted, 131, 85, 203, LV_TEXT_ALIGN_CENTER);

    // Real Android phone battery in the top-right only when available.
    battery_outline = lv_obj_create(screen);
    lv_obj_set_pos(battery_outline, 338, 88);
    lv_obj_set_size(battery_outline, 26, 14);
    lv_obj_set_style_radius(battery_outline, 2, 0);
    lv_obj_set_style_bg_opa(battery_outline, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_outline, 2, 0);
    lv_obj_set_style_border_color(battery_outline, lv_color_hex(kMuted), 0);
    lv_obj_set_style_pad_all(battery_outline, 0, 0);
    lv_obj_remove_flag(battery_outline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(battery_outline, LV_OBJ_FLAG_CLICKABLE);
    // Filled level bar overlaid on the border's interior.
    battery_fill = lv_obj_create(screen);
    lv_obj_set_pos(battery_fill, 342, 92);
    lv_obj_set_size(battery_fill, 0, 6);
    lv_obj_set_style_bg_color(battery_fill, lv_color_hex(kWhite), 0);
    lv_obj_set_style_bg_opa(battery_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(battery_fill, 0, 0);
    lv_obj_set_style_border_width(battery_fill, 0, 0);
    lv_obj_set_style_pad_all(battery_fill, 0, 0);
    lv_obj_remove_flag(battery_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(battery_fill, LV_OBJ_FLAG_CLICKABLE);
    battery_percent = make_label(screen, "", &lv_font_montserrat_16,
                                 kMuted, 371, 84, 50, LV_TEXT_ALIGN_LEFT);

    // User-approved 112x132 arrow, now pushed left by 39px. Distance below it.
    maneuver_graphic = lv_image_create(screen);
    lv_obj_set_pos(maneuver_graphic, 44, 116);
    lv_obj_set_size(maneuver_graphic, 112, 132);
    lv_obj_set_style_image_recolor(maneuver_graphic, lv_color_hex(kBlue), 0);
    lv_obj_set_style_image_recolor_opa(maneuver_graphic, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(maneuver_graphic, 0, 0);
    lv_obj_set_style_pad_all(maneuver_graphic, 0, 0);
    lv_obj_remove_flag(maneuver_graphic, LV_OBJ_FLAG_SCROLLABLE);

    distance = make_label(screen, "--", &lv_font_montserrat_40,
                          kWhite, 24, 252, 171, LV_TEXT_ALIGN_CENTER);

    // Street moves left by 22px and gains 54px width versus old layout.
    // Four lines for long roads, no overlap with the bottom AFR divider.
    street = make_label(screen, "Start navigation", &lv_font_montserrat_32,
                        kWhite, 211, 133, 224, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_size(street, 224, 162);
    lv_label_set_long_mode(street, LV_LABEL_LONG_DOT);

    // AEM-inspired, near-rim 30-segment display; blue is decorative until
    // actual wideband measurements and meaningful thresholds exist.
    afr_label = make_label(screen, "WIDEBAND AFR", &lv_font_montserrat_20,
                           kBlue, 115, 335, 236, LV_TEXT_ALIGN_CENTER);
    afr_value = make_label(screen, "--.-", &lv_font_montserrat_48,
                           kWhite, 104, 363, 258, LV_TEXT_ALIGN_CENTER);

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
