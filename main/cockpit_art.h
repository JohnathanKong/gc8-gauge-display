#pragma once
// GC8 cockpit illustration assets. A8 sprites are rasterized once at boot;
// LVGL composites them with blue, red or white recoloring.
// These are graphic assets, not live vehicle measurements.
#include <cmath>
#include <cstdint>
#include "lvgl.h"

namespace cockpit_art {
constexpr int road_w = 320, road_h = 82;
constexpr int arc_w = 96, arc_h = 96;
constexpr int flag_w = 34, flag_h = 36;
constexpr int battery_w = 26, battery_h = 14;
// AEM-inspired 24-segment AFR sweep around the lower readout, inactive
// until an actual calibrated AFR sensor is connected. No invented needle,
// safety range, live AFR value or LVGL arc knob.
constexpr int sweep_w = 280, sweep_h = 126;

static uint8_t road_pixels[road_w * road_h] = {};
static uint8_t left_arc_pixels[arc_w * arc_h] = {};
static uint8_t right_arc_pixels[arc_w * arc_h] = {};
static uint8_t flag_pixels[flag_w * flag_h] = {};
static uint8_t battery_pixels[battery_w * battery_h] = {};
static uint8_t sweep_pixels[sweep_w * sweep_h] = {};

static const lv_image_dsc_t road = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,
               .flags = 0, .w = road_w, .h = road_h, .stride = road_w, .reserved_2 = 0},
    .data_size = sizeof(road_pixels), .data = road_pixels,
    .reserved = nullptr, .reserved_2 = nullptr
};
static const lv_image_dsc_t left_arc = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,
               .flags = 0, .w = arc_w, .h = arc_h, .stride = arc_w, .reserved_2 = 0},
    .data_size = sizeof(left_arc_pixels), .data = left_arc_pixels,
    .reserved = nullptr, .reserved_2 = nullptr
};
static const lv_image_dsc_t right_arc = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,
               .flags = 0, .w = arc_w, .h = arc_h, .stride = arc_w, .reserved_2 = 0},
    .data_size = sizeof(right_arc_pixels), .data = right_arc_pixels,
    .reserved = nullptr, .reserved_2 = nullptr
};
static const lv_image_dsc_t afr_sweep = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,
               .flags = 0, .w = sweep_w, .h = sweep_h,
               .stride = sweep_w, .reserved_2 = 0},
    .data_size = sizeof(sweep_pixels), .data = sweep_pixels,
    .reserved = nullptr, .reserved_2 = nullptr
};
static const lv_image_dsc_t finish_flag = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,
               .flags = 0, .w = flag_w, .h = flag_h, .stride = flag_w, .reserved_2 = 0},
    .data_size = sizeof(flag_pixels), .data = flag_pixels,
    .reserved = nullptr, .reserved_2 = nullptr
};
static const lv_image_dsc_t battery_icon = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,
               .flags = 0, .w = battery_w, .h = battery_h, .stride = battery_w, .reserved_2 = 0},
    .data_size = sizeof(battery_pixels), .data = battery_pixels,
    .reserved = nullptr, .reserved_2 = nullptr
};

inline uint8_t clamp_alpha(float value) {
    if (value < 0.0f) return 0;
    if (value > 255.0f) return 255;
    return static_cast<uint8_t>(value);
}
inline float bell(float distance, float width) {
    const float normalized = distance / width;
    return std::exp(-normalized * normalized);
}

inline void paint_road() {
    // Vanishing-point landscape: floor haze, shoulders, lane lines, and
    // perspective-dashed centerline, each with antialiased falloff.
    for (int y = 0; y < road_h; ++y) {
        const float t = static_cast<float>(y + 2) / road_h;
        const float half_width = 4.0f + 153.0f * t * t;
        for (int x = 0; x < road_w; ++x) {
            const float dx = std::fabs(static_cast<float>(x) - road_w / 2.0f);
            float a = 17.0f * bell(dx, 76.0f) * bell(static_cast<float>(y - 7), 24.0f);
            if (dx < half_width) {
                // Visible but subdued asphalt volume, dark enough for labels.
                a += 9.0f + 14.0f * t;
            }
            a += (50.0f + 63.0f * t) *
                 bell(dx - half_width * 0.91f, 0.8f + 1.7f * t);
            a += (23.0f + 47.0f * t) *
                 bell(dx - half_width * 0.53f, 0.8f + 1.2f * t);
            // Center dashes grow in length with perspective, separated by gaps.
            const bool painted = (y > 29 && y < 45) || (y > 54 && y < 78);
            if (painted) {
                a += (55.0f + 50.0f * t) *
                     bell(dx, 0.7f + 2.0f * t);
            }
            road_pixels[y * road_w + x] = clamp_alpha(a);
        }
    }
}

inline void paint_arc(uint8_t *pixels, int left, float start_angle, float end_angle) {
    constexpr float pi = 3.14159265359f;
    for (int y = 0; y < arc_h; ++y) {
        for (int x = 0; x < arc_w; ++x) {
            const float dx = static_cast<float>(x + left - 233);
            const float dy = static_cast<float>(y + 321 - 233);
            const float degrees = std::atan2(dy, dx) * 180.0f / pi;
            if (degrees < start_angle || degrees > end_angle) continue;
            const float radius = std::sqrt(dx * dx + dy * dy);
            const float ring = std::fabs(radius - 199.0f);
            float a = 45.0f * bell(ring, 7.0f);
            // Deliberate illuminated 5-degree sections, like instrument ticks.
            const int segment = static_cast<int>((degrees - start_angle) / 5.5f);
            const float within = (degrees - start_angle) - segment * 5.5f;
            if (within < 4.8f) a += 205.0f * bell(ring, 3.5f);
            pixels[y * arc_w + x] = clamp_alpha(a);
        }
    }
}

inline void paint_flag() {
    for (int y = 0; y < flag_h; ++y) {
        for (int x = 0; x < flag_w; ++x) {
            uint8_t value = 0;
            // Pole and finial.
            if (x >= 3 && x <= 5 && y >= 2 && y <= 34) value = 238;
            if (x >= 2 && x <= 6 && y >= 0 && y <= 3) value = 255;
            // Waving four-column checkered pennant, three rows.
            if (y >= 5 && y < 26) {
                const int shift = (y / 7) % 2;
                const int cx = x - 7 - shift;
                if (cx >= 0 && cx < 24) {
                    const int col = cx / 6;
                    const int row = (y - 5) / 7;
                    if (((col + row) % 2) == 0) value = 255;
                    else value = 65;
                }
            }
            flag_pixels[y * flag_w + x] = value;
        }
    }
}
inline void paint_battery() {
    for (int y = 0; y < battery_h; ++y) {
        for (int x = 0; x < battery_w; ++x) {
            const bool outline = (x >= 1 && x <= 22 && (y == 1 || y == 12)) ||
                                 ((x == 1 || x == 22) && y >= 1 && y <= 12);
            const bool terminal = x >= 23 && x <= 25 && y >= 5 && y <= 9;
            battery_pixels[y * battery_w + x] = (outline || terminal) ? 200 : 0;
        }
    }
}
// Discrete LED-like rectangular blocks around the lower AFR gauge sweep.
// The physical AEM X-Series uses an LED-sweep reference; our current sweep is
// neutral blue and does not display an artificial sensor position/value.
// Paint once at boot into an A8 texture, so the LVGL refresh stays cheap.
inline void paint_afr_sweep() {
    constexpr float pi = 3.14159265359f;
    constexpr float rx = 130.0f;
    constexpr float ry = 116.0f;
    constexpr float center_x = (sweep_w - 1) * 0.5f;
    constexpr int segments = 24;
    for (int y = 0; y < sweep_h; ++y) {
        const float ny = static_cast<float>(y) / ry;
        for (int x = 0; x < sweep_w; ++x) {
            const float nx = (static_cast<float>(x) - center_x) / rx;
            const float angle = std::atan2(ny, nx);
            if (angle < 0.0f || angle > pi) continue;
            const float radius = std::sqrt(nx * nx + ny * ny);
            const float distance = (radius - 1.0f) * 119.0f;
            if (std::fabs(distance) > 13.0f) continue;
            const float position = angle * segments / pi;
            const int segment = static_cast<int>(position);
            const float within = position - segment;
            // Faint connecting base and 24 individual, gently illuminated
            // blocks. Every sixth mark is stronger like a gauge graduation.
            float alpha = 24.0f * bell(distance, 8.5f);
            if (within >= 0.16f && within <= 0.84f) {
                const float brightness = segment % 6 == 0 ? 175.0f : 125.0f;
                alpha += std::fabs(distance) <= 4.0f
                    ? brightness
                    : brightness * bell(std::fabs(distance) - 4.0f, 2.2f);
            }
            sweep_pixels[y * sweep_w + x] = clamp_alpha(alpha);
        }
    }
}
inline void rasterize() {
    static bool initialized = false;
    if (initialized) return;
    paint_road();
    paint_arc(left_arc_pixels, 47, 123.0f, 150.0f);
    paint_arc(right_arc_pixels, 323, 30.0f, 57.0f);
    paint_flag();
    paint_battery();
    paint_afr_sweep();
    initialized = true;
}
} // namespace cockpit_art
