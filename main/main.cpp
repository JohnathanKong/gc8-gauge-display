#include <cstdio>
#include <cstring>
#include <cmath>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs.h"
#include "gc8_ble.h"
#include "device_power.h"
#include "device_orientation.h"
#include "garage_button.h"
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
lv_obj_t *maneuver_glow[4] = {};
lv_obj_t *afr_value = nullptr;
lv_obj_t *battery_percent = nullptr;
lv_obj_t *battery_fill = nullptr;
lv_obj_t *battery_outline = nullptr;
lv_obj_t *battery_tip = nullptr;
lv_obj_t *nav_separator = nullptr;
lv_obj_t *idle_panel = nullptr; // Invisible parent; no background card.
lv_obj_t *idle_bounce = nullptr;
lv_obj_t *idle_dot = nullptr;
lv_obj_t *idle_dot_halo = nullptr;
bool idle_bounce_active = false;
lv_obj_t *idle_heading = nullptr;
lv_obj_t *idle_title = nullptr;
lv_obj_t *idle_hint = nullptr;
lv_obj_t *garage_toast = nullptr;
lv_obj_t *garage_toast_text = nullptr;
bool garage_toast_visible = false;
char garage_previous_text[32] = {};
int last_device_battery = -2;
bool last_battery_charging = false;
bool last_external_power = false, last_power_status_valid = false;
int applied_brightness = -1;
lv_obj_t *afr_sweep_sprite = nullptr;
lv_obj_t *afr_label = nullptr;
Gc8BleSnapshot previous = {};
uint32_t boot_count = 0;
esp_reset_reason_t boot_reason = ESP_RST_UNKNOWN;
bool first_update = true;
uint8_t applied_orientation = 0; // Native CO5300 starts unrotated.

const char *reset_reason_text(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "POWER ON";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT WDT";
        case ESP_RST_TASK_WDT: return "TASK WDT";
        case ESP_RST_WDT: return "WATCHDOG";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_PWR_GLITCH: return "POWER GLITCH";
        case ESP_RST_USB: return "USB RESET";
        case ESP_RST_CPU_LOCKUP: return "CPU LOCKUP";
        case ESP_RST_DEEPSLEEP: return "DEEP SLEEP";
        case ESP_RST_EXT: return "EXTERNAL";
        default: return "OTHER";
    }
}

void count_device_boot() {
    boot_reason = esp_reset_reason();
    nvs_handle_t handle = 0;
    if (nvs_open("gc8_boot", NVS_READWRITE, &handle) != ESP_OK) return;
    uint32_t previous_boots = 0;
    nvs_get_u32(handle, "count", &previous_boots);
    boot_count = previous_boots + 1;
    if (nvs_set_u32(handle, "count", boot_count) == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Boot count=%u reset=%s", static_cast<unsigned>(boot_count),
             reset_reason_text(boot_reason));
}

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
lv_obj_t *add_rule(lv_obj_t *screen, int x, int y, int width, int height,
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
    return rule;
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
        // Very soft 4-direction glow behind the approved A8 silhouette.
        // The real arrow itself is NOT resized or distorted.
        for (auto *halo : maneuver_glow) {
            lv_image_set_src(halo, source);
            lv_obj_remove_flag(halo, LV_OBJ_FLAG_HIDDEN);
        }
        lv_image_set_src(maneuver_graphic, source);
        lv_obj_remove_flag(maneuver_graphic, LV_OBJ_FLAG_HIDDEN);
    } else {
        for (auto *halo : maneuver_glow) {
            lv_obj_add_flag(halo, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(maneuver_graphic, LV_OBJ_FLAG_HIDDEN);
    }
}

// Called by the same LVGL task as navigation. No BLE/UI data races,
// no allocations or expensive artwork redraws on animation frames.
void animate_idle_bounce(lv_timer_t *) {
    if (!idle_bounce_active || !idle_dot || !idle_dot_halo) return;
    // One smooth out-and-back traversal every 3.6 seconds. The moving light
    // eases at both ends instead of snapping at the display rim.
    constexpr float pi = 3.14159265358979323846f;
    const int64_t ms = esp_timer_get_time() / 1000;
    const float phase = static_cast<float>(ms % 3600) / 3600.f;
    const float travel = 0.5f - 0.5f * std::cos(2.f * pi * phase);
    const int centre_x = 72 + static_cast<int>(322.f * travel + 0.5f);
    lv_obj_set_x(idle_dot_halo, centre_x - 20);
    lv_obj_set_x(idle_dot, centre_x - 7);
}

void refresh_navigation(lv_timer_t *) {
    // This runs inside the BSP/LVGL GUI task: serialize controller commands
    // with its SPI flushes. The screen is square, so hardware quarter turns
    // rotate all artwork, labels and navigation arrows together.
    const uint8_t requested = device_orientation_rotation();
    if (requested != applied_orientation) {
        const bsp_display_rotation_t selected = requested == 1 ? BSP_DISPLAY_ROTATE_90 :
            requested == 2 ? BSP_DISPLAY_ROTATE_180 :
            requested == 3 ? BSP_DISPLAY_ROTATE_270 : BSP_DISPLAY_ROTATE_0;
        const esp_err_t err = bsp_display_rotation_set(selected);
        if (err == ESP_OK) {
            applied_orientation = requested;
            lv_obj_invalidate(lv_screen_active());
            ESP_LOGI(TAG, "CO5300 display orientation=%u deg",
                     static_cast<unsigned>(requested * 90));
        } else {
            ESP_LOGW(TAG, "Display orientation failed: %s", esp_err_to_name(err));
        }
    }
    const auto current = gc8_ble_snapshot();
    // Apply display-only idle dimming even if the nav snapshot didn't change.
    // Keep the ESP32 fully awake: garage PWR, HID voice and BLE still work.
    const uint8_t target_brightness =
        device_power_target_brightness(current.route_active, false);
    if (target_brightness != applied_brightness &&
        bsp_display_brightness_set(target_brightness) == ESP_OK) {
        applied_brightness = target_brightness;
        ESP_LOGI(TAG, "Panel brightness %u%% (route=%d diagnostic=%d)",
                 static_cast<unsigned>(target_brightness),
                 static_cast<int>(current.route_active),
                 0);
    }

    // Evaluate the transient overlay BEFORE the snapshot-change guard, so
    // it disappears on time even when Maps/BLE are completely idle.
    const int64_t now_us = esp_timer_get_time();
    const bool garage_active = current.garage_banner[0] &&
                              now_us < current.garage_banner_until_us;
    const bool banner_active = garage_active;
    const char *banner_message = current.garage_banner;
    if (banner_active) {
        if (!garage_toast_visible ||
            std::strcmp(garage_previous_text, banner_message) != 0) {
            lv_label_set_text(garage_toast_text, banner_message);
            std::snprintf(garage_previous_text, sizeof(garage_previous_text),
                          "%s", banner_message);
            lv_obj_remove_flag(garage_toast, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(garage_toast);
            // Slide down over the navigation for a few seconds. Clock and
            // existing full-circle dashboard are not modified or redrawn.
            lv_anim_delete(garage_toast, nullptr);
            lv_anim_t slide;
            lv_anim_init(&slide);
            lv_anim_set_var(&slide, garage_toast);
            lv_anim_set_values(&slide, -64, 113);
            lv_anim_set_duration(&slide, 230);
            lv_anim_set_exec_cb(&slide, [](void *obj, int32_t y) {
                lv_obj_set_y(static_cast<lv_obj_t *>(obj), y);
            });
            lv_anim_start(&slide);
        }
        garage_toast_visible = true;
    } else if (garage_toast_visible) {
        garage_toast_visible = false;
        lv_anim_delete(garage_toast, nullptr);
        lv_obj_add_flag(garage_toast, LV_OBJ_FLAG_HIDDEN);
    }

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
    // The PMIC changes independently of BLE packets, so refresh its icon
    // even when the navigation snapshot hasn't changed in many minutes.
    const auto power = device_power_snapshot();
    const int power_percent = power.valid ? static_cast<int>(power.percent) : -1;
    const bool power_changed = power_percent != last_device_battery ||
                               power.charging != last_battery_charging ||
                               power.external_power != last_external_power ||
                               power.vbus_valid != last_power_status_valid;
    if (!first_update && std::memcmp(&previous, &current, sizeof(current)) == 0 &&
        !power_changed) return;
    first_update = false;
    previous = current;

    lv_label_set_text(connection,
        current.connected ? "BLE CONNECTED" : "BLE WAITING");
    // Real AXP2101 fuel-gauge data from the WAVE SHARE DEVICE, not phone.
    if (power_changed) {
        last_device_battery = power_percent;
        last_battery_charging = power.charging;
        last_external_power = power.external_power;
        last_power_status_valid = power.vbus_valid;
        if (power.valid) {
            char pct[8];
            std::snprintf(pct, sizeof(pct), "%u%%",
                          static_cast<unsigned>(power.percent));
            lv_label_set_text(battery_percent, pct);
            lv_obj_set_width(battery_fill, power.percent == 0 ? 1 :
                             static_cast<int>(power.percent) * 17 / 100);
            const uint32_t power_color = power.charging ? kBlue : kWhite;
            lv_obj_set_style_bg_color(battery_fill, lv_color_hex(power_color), 0);
            lv_obj_remove_flag(battery_percent, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(battery_outline, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(battery_tip, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(battery_fill, LV_OBJ_FLAG_HIDDEN);
        } else {
            // No installed battery must not be misrepresented as 0%.
            // USB power can still be independently known from PMIC status.
            if (power.vbus_valid && power.external_power) {
                lv_label_set_text(battery_percent, "USB");
                lv_obj_remove_flag(battery_percent, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(battery_percent, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_add_flag(battery_outline, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(battery_tip, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(battery_fill, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (current.connected && current.has_demo_afr) {
        lv_label_set_text(afr_value, current.demo_afr);
        lv_label_set_text(afr_label, "AFR DEMO");
    } else {
        lv_label_set_text(afr_value, "--.-");
        lv_label_set_text(afr_label, "AFR");
    }

    // With a valid Maps maneuver, show the ORIGINAL navigation UI.
    // The grid never covers arrows, roads, distances or AFR.
    const bool has_turn = current.connected && current.has_navigation;
    if (has_turn) {
        idle_bounce_active = false;
        lv_obj_add_flag(idle_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(distance, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(street, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(nav_separator, LV_OBJ_FLAG_HIDDEN);
        draw_arrow(current.maneuver);
        lv_label_set_text(distance, current.distance);
        lv_obj_set_style_text_font(street,
            std::strlen(current.road) > 27 ? &lv_font_montserrat_26 :
                                            &lv_font_montserrat_32, 0);
        lv_label_set_text(street, current.road);
    } else {
        draw_arrow("");
        lv_obj_add_flag(distance, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(street, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(nav_separator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(idle_panel, LV_OBJ_FLAG_HIDDEN);

        // Only the former READY state becomes a bouncing signal + grid.
        // Connection errors and in-progress Maps guidance still have their
        // existing information, but without the old visible container.
        const bool standby = current.connected && !current.route_active;
        idle_bounce_active = standby;
        if (standby) {
            lv_obj_remove_flag(idle_bounce, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(idle_heading, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(idle_title, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(idle_hint, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(idle_bounce, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(idle_heading, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(idle_title, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(idle_hint, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_font(idle_title, &lv_font_montserrat_40, 0);
            if (!current.connected) {
                lv_label_set_text(idle_heading, "GC8 COMPANION");
                lv_label_set_text(idle_title, "CONNECT PHONE");
                lv_label_set_text(idle_hint, "OPEN COMPANION TO PAIR");
            } else if (current.route_active && current.guidance_title[0]) {
                lv_label_set_text(idle_heading, "GOOGLE MAPS");
                lv_obj_set_style_text_font(idle_title,
                    std::strlen(current.guidance_title) > 16
                        ? &lv_font_montserrat_26 : &lv_font_montserrat_32, 0);
                lv_label_set_text(idle_title, current.guidance_title);
                lv_label_set_text(idle_hint,
                    current.guidance_hint[0] ? current.guidance_hint : "FOLLOW MAPS");
            } else if (current.route_active && current.guidance_stale) {
                lv_label_set_text(idle_heading, "GOOGLE MAPS");
                lv_label_set_text(idle_title, "GUIDANCE PAUSED");
                lv_label_set_text(idle_hint, "WAITING FOR MAPS");
            } else {
                lv_label_set_text(idle_heading, "GOOGLE MAPS");
                lv_label_set_text(idle_title, "ROUTE STARTED");
                lv_label_set_text(idle_hint, "WAITING FOR FIRST TURN");
            }
        }
    }
}

void draw_home() {
    auto *screen = lv_screen_active();
    lv_obj_clean(screen);
    // Display only. All settings and diagnostics are in GC8 Companion.
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBlack), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Reference-matched 466px round cockpit. Layers: atmospheric road,
    // segmented rim gauge, fine dividers, then white text and blue arrow.
    if (cockpit_art::road.data) {
        add_sprite(screen, &cockpit_art::road,
                   cockpit_art::road_x, cockpit_art::road_y, kBlue,
                   LV_OPA_90);
    }
    if (cockpit_art::afr_sweep.data) {
        afr_sweep_sprite = add_sprite(screen, &cockpit_art::afr_sweep,
                                     cockpit_art::sweep_x,
                                     cockpit_art::sweep_y, 0x126CEC);
    }
    if (cockpit_art::afr_tile_light.data) {
        add_sprite(screen, &cockpit_art::afr_tile_light,
                   cockpit_art::sweep_x, cockpit_art::sweep_y, 0x94C9FF);
    }
    // Draw the two rim highlights ABOVE atmospheric artwork, not beneath it.
    add_rim(screen, 5, 0x49667B, LV_OPA_70);
    add_rim(screen, 11, 0x1D3856, LV_OPA_50);

    // All reference separators are intentional: upper-left and upper-right
    // rule fragments, vertical arrow/street separator, and AFR horizon line.
    add_rule(screen, 48, 79, 93, 1, 0x2A5474, LV_OPA_60);
    add_rule(screen, 309, 79, 31, 1, 0x2A5474, LV_OPA_60);
    nav_separator = add_rule(screen, 194, 120, 2, 140, kBlue, LV_OPA_50);
    add_rule(screen, 42, 310, 382, 2, kBlue, LV_OPA_80);

    // Phone-clock, not an invented ESP32 date: "--:--" until first sync.
    clock_label = make_label(screen, "--:--", &lv_font_montserrat_40,
                             kBlue, 118, 26, 230, LV_TEXT_ALIGN_CENTER);
    connection = make_label(screen, "BLE WAITING", &lv_font_montserrat_16,
                            kMuted, 131, 85, 203, LV_TEXT_ALIGN_CENTER);

    // Real Waveshare battery in the top-right, only with a valid AXP2101 reading.
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
    // Small exposed terminal completes the Waveshare battery icon.
    battery_tip = lv_obj_create(screen);
    lv_obj_set_pos(battery_tip, 365, 93);
    lv_obj_set_size(battery_tip, 3, 5);
    lv_obj_set_style_bg_color(battery_tip, lv_color_hex(kMuted), 0);
    lv_obj_set_style_bg_opa(battery_tip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(battery_tip, 0, 0);
    lv_obj_set_style_radius(battery_tip, 0, 0);
    lv_obj_set_style_pad_all(battery_tip, 0, 0);
    lv_obj_remove_flag(battery_tip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(battery_tip, LV_OBJ_FLAG_CLICKABLE);

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
    // Low-opacity silhouettes underneath provide a neon edge, not a larger arrow.
    const int glow_dx[4] = {-3, 3, 0, 0};
    const int glow_dy[4] = {0, 0, -3, 3};
    for (int i = 0; i < 4; ++i) {
        auto *halo = lv_image_create(screen);
        maneuver_glow[i] = halo;
        lv_obj_set_pos(halo, 44 + glow_dx[i], 116 + glow_dy[i]);
        lv_obj_set_size(halo, 112, 132);
        lv_obj_set_style_image_recolor(halo, lv_color_hex(kBlue), 0);
        lv_obj_set_style_image_recolor_opa(halo, LV_OPA_COVER, 0);
        lv_obj_set_style_image_opa(halo, LV_OPA_20, 0);
        lv_obj_set_style_border_width(halo, 0, 0);
        lv_obj_set_style_pad_all(halo, 0, 0);
        lv_obj_remove_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(halo, LV_OBJ_FLAG_CLICKABLE);
    }
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

    // Invisible grouping parent ONLY. Do not bring back the READY card,
    // its border, background fill, status header, or decorative dividers.
    idle_panel = lv_obj_create(screen);
    lv_obj_set_pos(idle_panel, 0, 118);
    lv_obj_set_size(idle_panel, 466, 185);
    lv_obj_set_style_bg_opa(idle_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(idle_panel, 0, 0);
    lv_obj_set_style_radius(idle_panel, 0, 0);
    lv_obj_set_style_pad_all(idle_panel, 0, 0);
    lv_obj_remove_flag(idle_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(idle_panel, LV_OBJ_FLAG_CLICKABLE);

    // Grid Bounce replaces the READY text only. Nothing here covers the
    // independent 30-segment AFR arc at the bottom of the display.
    idle_bounce = lv_obj_create(idle_panel);
    lv_obj_set_pos(idle_bounce, 0, 0);
    lv_obj_set_size(idle_bounce, 466, 185);
    lv_obj_set_style_bg_opa(idle_bounce, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(idle_bounce, 0, 0);
    lv_obj_set_style_pad_all(idle_bounce, 0, 0);
    lv_obj_remove_flag(idle_bounce, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(idle_bounce, LV_OBJ_FLAG_CLICKABLE);
    if (cockpit_art::idle_grid.data) {
        add_sprite(idle_bounce, &cockpit_art::idle_grid,
                   cockpit_art::grid_x, cockpit_art::grid_y - 118,
                   0x2C8AFF, LV_OPA_60);
    }
    // The fine full-width line is stationary; only the glow moves.
    add_rule(idle_bounce, 64, 86, 338, 2, 0x269FFF, LV_OPA_70);
    idle_dot_halo = lv_obj_create(idle_bounce);
    lv_obj_set_pos(idle_dot_halo, 52, 66);
    lv_obj_set_size(idle_dot_halo, 40, 40);
    lv_obj_set_style_radius(idle_dot_halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(idle_dot_halo, lv_color_hex(0x198BFF), 0);
    lv_obj_set_style_bg_opa(idle_dot_halo, LV_OPA_20, 0);
    lv_obj_set_style_border_width(idle_dot_halo, 0, 0);
    lv_obj_set_style_pad_all(idle_dot_halo, 0, 0);
    lv_obj_remove_flag(idle_dot_halo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(idle_dot_halo, LV_OBJ_FLAG_CLICKABLE);
    idle_dot = lv_obj_create(idle_bounce);
    lv_obj_set_pos(idle_dot, 65, 79);
    lv_obj_set_size(idle_dot, 14, 14);
    lv_obj_set_style_radius(idle_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(idle_dot, lv_color_hex(0xD8F5FF), 0);
    lv_obj_set_style_bg_opa(idle_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(idle_dot, 0, 0);
    lv_obj_set_style_shadow_color(idle_dot, lv_color_hex(0x168BFF), 0);
    lv_obj_set_style_shadow_opa(idle_dot, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(idle_dot, 15, 0);
    lv_obj_set_style_pad_all(idle_dot, 0, 0);
    lv_obj_remove_flag(idle_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(idle_dot, LV_OBJ_FLAG_CLICKABLE);

    // Keep route-acquisition and pairing messages functional, without a
    // boxed dashboard panel; the text is hidden entirely in idle standby.
    idle_heading = make_label(idle_panel, "", &lv_font_montserrat_20,
                              kBlue, 109, 19, 248, LV_TEXT_ALIGN_CENTER);
    idle_title = make_label(idle_panel, "", &lv_font_montserrat_40,
                            kWhite, 25, 63, 416, LV_TEXT_ALIGN_CENTER);
    idle_hint = make_label(idle_panel, "", &lv_font_montserrat_20,
                           kMuted, 25, 137, 416, LV_TEXT_ALIGN_CENTER);

    // Keep the large dynamic AFR value ABOVE the 30-segment arc and the
    // compact AFR caption BELOW it, in both navigation and idle scenes.
    // Leave the true concentric rails and scale labels untouched.
    // --.- remains until BLE supplies a demo or future live sensor reading.
    afr_value = make_label(screen, "--.-", &lv_font_montserrat_44,
                           kWhite, 150, 345, 166, LV_TEXT_ALIGN_CENTER);
    afr_label = make_label(screen, "AFR", &lv_font_montserrat_20,
                           kWhite, 157, 433, 152, LV_TEXT_ALIGN_CENTER);
    make_label(screen, "10", &lv_font_montserrat_16,
               0xA4D6FF, 83, 398, 46, LV_TEXT_ALIGN_CENTER);
    make_label(screen, "20", &lv_font_montserrat_16,
               0xA4D6FF, 337, 398, 46, LV_TEXT_ALIGN_CENTER);

    // A small drop-down overlay for upper PWR / Meross requests. Only an
    // acknowledged command can say SENT; local button receipt says REQUESTED.
    // Stays independent of both navigation and AFR screen states.
    garage_toast = lv_obj_create(screen);
    lv_obj_set_pos(garage_toast, 56, -64);
    lv_obj_set_size(garage_toast, 354, 62);
    lv_obj_set_style_radius(garage_toast, 14, 0);
    lv_obj_set_style_bg_color(garage_toast, lv_color_hex(0x07182A), 0);
    lv_obj_set_style_bg_opa(garage_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(garage_toast, lv_color_hex(kBlue), 0);
    lv_obj_set_style_border_opa(garage_toast, LV_OPA_80, 0);
    lv_obj_set_style_border_width(garage_toast, 2, 0);
    lv_obj_set_style_pad_all(garage_toast, 0, 0);
    lv_obj_remove_flag(garage_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(garage_toast, LV_OBJ_FLAG_CLICKABLE);
    garage_toast_text = make_label(garage_toast, "", &lv_font_montserrat_20,
                                  kWhite, 10, 19, 334, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(garage_toast, LV_OBJ_FLAG_HIDDEN);

    lv_timer_create(refresh_navigation, 250, nullptr);
    lv_timer_create(animate_idle_bounce, 33, nullptr);
    refresh_navigation(nullptr);
}
} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting GC8 cockpit navigation dashboard");
    // Generate the PSRAM-backed artwork before acquiring LVGL's render lock.
    // No heavy atan2/exp loop runs inside the UI refresh/render critical path.
    cockpit_art::rasterize();
    bsp_display_start();
    bsp_display_lock(-1);
    draw_home();
    bsp_display_unlock();
    // BLE initialization also initializes NVS; orientation preferences must
    // load only AFTER NVS is ready. The IMU shares BSP I2C with the PMIC.
    gc8_ble_start();
    count_device_boot(); // Once per reboot, after NimBLE has initialized NVS.
    device_orientation_start();
    device_power_start(); // Sample genuine battery on BSP's shared I2C bus.
    garage_button_start(); // Upper PWR=garage; short lower BOOT=HID voice (GPIO0 stays flash strap).
}
