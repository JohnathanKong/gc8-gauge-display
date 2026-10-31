#pragma once
// GC8 cockpit artwork, rasterized ONCE on startup into PSRAM-backed A8 masks.
// Text, clock, battery, navigation and AFR data remain LVGL objects / real BLE
// state; these masks contain no static instrument values or fake sensor data.
#include <cmath>
#include <cstdint>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

namespace cockpit_art {
constexpr int road_w = 466, road_h = 151;
constexpr int road_x = 0, road_y = 175;
constexpr int sweep_w = 466, sweep_h = 149;
constexpr int sweep_x = 0, sweep_y = 305;

static lv_image_dsc_t road = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,
               .flags = 0, .w = road_w, .h = road_h,
               .stride = road_w, .reserved_2 = 0},
    .data_size = road_w * road_h, .data = nullptr,
    .reserved = nullptr, .reserved_2 = nullptr
};
static lv_image_dsc_t afr_sweep = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8,
               .flags = 0, .w = sweep_w, .h = sweep_h,
               .stride = sweep_w, .reserved_2 = 0},
    .data_size = sweep_w * sweep_h, .data = nullptr,
    .reserved = nullptr, .reserved_2 = nullptr
};

inline uint8_t alpha_clamp(float a) {
    if (a <= 0.f) return 0;
    if (a >= 255.f) return 255;
    return static_cast<uint8_t>(a + 0.5f);
}
inline float glow(float dx, float sigma) {
    const float z = dx / sigma;
    return std::exp(-z * z);
}

inline bool allocate_image(lv_image_dsc_t &image) {
    // These two masks total ~140 KiB. Explicit PSRAM allocation avoids
    // exhausting the ESP32-S3's scarce internal BLE/LVGL RAM.
    auto *buffer = static_cast<uint8_t *>(heap_caps_malloc(
        image.data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer) {
        ESP_LOGE("gc8_art", "PSRAM allocation failed for %u bytes",
                 static_cast<unsigned>(image.data_size));
        return false;
    }
    std::memset(buffer, 0, image.data_size);
    image.data = buffer;
    return true;
}

inline void paint_road() {
    if (!road.data) return;
    auto *pixels = const_cast<uint8_t *>(road.data);
    // Reference-inspired road sweeping almost rim-to-rim. Horizon at y~228;
    // lane markings converge at (233,228) and spread toward y=323.
    constexpr float cx = 233.f;
    for (int y = 0; y < road_h; ++y) {
        const float h = static_cast<float>(y) - 53.f;
        const float t = h > 0.f ? h / (road_h - 53.f) : 0.f;
        const float reach = 220.f * std::pow(t, 0.89f);
        const float inner = 92.f * std::pow(t, 0.96f);
        const bool dash = (y > 72 && y < 88) ||
                          (y > 98 && y < 119) ||
                          (y > 128 && y < 149);
        for (int x = 0; x < road_w; ++x) {
            const float dx = std::fabs(static_cast<float>(x) - cx);
            float a = 11.f * glow(dx, 180.f) *
                      glow(static_cast<float>(y) - 61.f, 28.f);
            if (t > 0.f) {
                if (dx < reach) a += 7.f + 13.f * t;
                a += (95.f + 135.f * t) *
                     glow(dx - reach * 0.92f, 1.0f + 1.8f * t);
                a += (31.f + 94.f * t) *
                     glow(dx - inner, 1.1f + 1.5f * t);
                if (dash) a += (75.f + 165.f * t) *
                    glow(dx, 0.8f + 2.7f * t);
                // Wide, subtle horizon on either side of the road.
                a += 19.f * glow(static_cast<float>(y) - 70.f, 15.f) *
                     glow(dx - 170.f, 88.f);
            }
            pixels[y * road_w + x] = alpha_clamp(a);
        }
    }
}

inline void paint_afr_sweep() {
    if (!afr_sweep.data) return;
    auto *pixels = const_cast<uint8_t *>(afr_sweep.data);
    // This is a near-rim lower arc of the actual 466px CIRCLE,
    // not a U-shaped graphic constrained to the center 280px.
    // Angle: 23° (right rim) to 157° (left rim), bottom at y~442.
    // All 30 segments are decorative/unselected until real wideband input.
    constexpr float pi = 3.14159265358979323846f;
    constexpr float degrees_to_rad = pi / 180.f;
    constexpr float first_angle = 23.f * degrees_to_rad;
    constexpr float last_angle = 157.f * degrees_to_rad;
    constexpr float sweep = last_angle - first_angle;
    constexpr float cx = 233.f, cy = 233.f;
    constexpr int count = 30;
    for (int y = 0; y < sweep_h; ++y) {
        const float dy = static_cast<float>(y + sweep_y) - cy;
        for (int x = 0; x < sweep_w; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float angle = std::atan2(dy, dx);
            if (angle < first_angle || angle > last_angle) continue;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float u = (angle - first_angle) / sweep * count;
            const int segment = static_cast<int>(u);
            const float fraction = u - segment;

            // Same near-rim, 30-segment geometry as the approved GC8
            // cockpit, now with a thicker bright-blue segmented band like
            // the selected reference. This is a STATIC illuminated scale:
            // never imply actual AFR measurements before AEM is connected.
            // All nav/idle scenes use this shared underlying artwork.
            float a = 23.f * glow(r - 216.f, 1.8f); // outer fine rail
            a += 22.f * glow(r - 187.f, 1.7f);     // inner rail
            if (fraction > 0.13f && fraction < 0.87f) {
                if (r >= 191.f && r <= 215.f) {
                    // Hard-edged luminous tiles separated by dark radial
                    // dividers. A subtle centre glow creates a filled bar.
                    a += 193.f + 48.f * glow(r - 203.f, 6.5f);
                    if (r > 209.f) a += 13.f;
                } else {
                    a += 38.f * glow(std::fabs(r - 203.f) - 12.f, 3.f);
                }
            }
            // Individual tick marks BELOW the bar, stronger every fifth
            // segment, without any rich/lean alarm colors.
            if (fraction < 0.10f && r >= 177.f && r < 189.f) {
                a += (segment % 5 == 0 ? 149.f : 81.f) *
                    glow(r - 183.f, 5.5f);
            }
            a += 17.f * glow(r - 203.f, 11.f); // restrained halo
            pixels[y * sweep_w + x] = alpha_clamp(a);
        }
    }
}

inline void rasterize() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    if (allocate_image(road)) paint_road();
    if (allocate_image(afr_sweep)) paint_afr_sweep();
}
} // namespace cockpit_art
