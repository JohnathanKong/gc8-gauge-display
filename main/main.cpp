#include "esp_log.h"
#include "gc8_ble.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

namespace {
constexpr auto *TAG = "gc8_display";
constexpr uint32_t kBackground = 0x070A0E;
constexpr uint32_t kSurface = 0x151B22;
constexpr uint32_t kMuted = 0x81909E;
constexpr uint32_t kWhite = 0xF4F6F8;
constexpr uint32_t kAccent = 0xF0B74A;
lv_obj_t *phone_status = nullptr;
lv_obj_t *nav_instruction = nullptr;
lv_obj_t *nav_road = nullptr;

void refresh_ble_ui(lv_timer_t *) {
    const auto state = gc8_ble_snapshot();
    if (!phone_status) return;
    lv_label_set_text(phone_status, state.connected ? "PHONE: CONNECTED" : "PHONE: WAITING");
    if (state.connected && state.has_navigation) {
        lv_label_set_text_fmt(nav_instruction, "NAV: %s  %s", state.maneuver, state.distance);
        lv_label_set_text(nav_road, state.road);
    } else {
        lv_label_set_text(nav_instruction, "NAV: NO DATA");
        lv_label_set_text(nav_road, "ECU: OFFLINE");
    }
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                     const lv_font_t *font, uint32_t color,
                     int x, int y, int width, lv_text_align_t alignment) {
    auto *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, alignment, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

void make_readout(lv_obj_t *screen, const char *name, const char *value,
                  int x, int y) {
    auto *tile = lv_obj_create(screen);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, 166, 79);
    lv_obj_set_style_radius(tile, 15, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(kSurface), 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    make_label(tile, name, &lv_font_montserrat_16, kMuted,
               0, 11, 166, LV_TEXT_ALIGN_CENTER);
    make_label(tile, value, &lv_font_montserrat_26, kWhite,
               0, 37, 166, LV_TEXT_ALIGN_CENTER);
}

void render_dashboard() {
    auto *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    make_label(screen, "SUBARU", &lv_font_montserrat_16, kMuted,
               0, 43, 466, LV_TEXT_ALIGN_CENTER);
    make_label(screen, "GC8", &lv_font_montserrat_26, kWhite,
               0, 75, 466, LV_TEXT_ALIGN_CENTER);
    auto *rule = lv_obj_create(screen);
    lv_obj_set_pos(rule, 177, 114);
    lv_obj_set_size(rule, 112, 3);
    lv_obj_set_style_bg_color(rule, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 2, 0);
    lv_obj_set_style_pad_all(rule, 0, 0);

    make_label(screen, "STI  /  VERSION 3", &lv_font_montserrat_16, kMuted,
               0, 135, 466, LV_TEXT_ALIGN_CENTER);
    make_readout(screen, "COOLANT", "-- C", 61, 181);
    make_readout(screen, "AFR", "--.-", 239, 181);
    make_readout(screen, "BOOST", "--.-", 61, 271);
    make_readout(screen, "RPM", "----", 239, 271);
    phone_status = make_label(screen, "PHONE: WAITING", &lv_font_montserrat_16,
                              kAccent, 0, 366, 466, LV_TEXT_ALIGN_CENTER);
    nav_instruction = make_label(screen, "NAV: NO DATA", &lv_font_montserrat_16,
                                 kWhite, 0, 392, 466, LV_TEXT_ALIGN_CENTER);
    nav_road = make_label(screen, "ECU: OFFLINE", &lv_font_montserrat_16,
                          kMuted, 0, 418, 466, LV_TEXT_ALIGN_CENTER);
    lv_timer_create(refresh_ble_ui, 250, nullptr);
}
}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting GC8 dashboard (Waveshare 1.75C)");
    bsp_display_start();
    bsp_display_lock(-1);
    render_dashboard();
    bsp_display_unlock();
    ESP_LOGI(TAG, "Dashboard initialized; telemetry intentionally unavailable");
    gc8_ble_start();
}
