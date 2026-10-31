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

// Separate light-blue tile layer: the underlying deep-blue mask provides the
// fine rails and bloom, while this mask gives each segment its icy centre.
static lv_image_dsc_t afr_tile_light = {
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
inline float clamp01(float v) {
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

inline bool allocate_image(lv_image_dsc_t &image) {
    // These three masks total ~205 KiB. Explicit PSRAM allocation avoids
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
    if (!afr_sweep.data || !afr_tile_light.data) return;
    auto *blue = const_cast<uint8_t *>(afr_sweep.data);
    auto *ice = const_cast<uint8_t *>(afr_tile_light.data);

    // Reference layout: 30 broad, individually illuminated tiles in a
    // shallow smile below the AFR title, NOT a near-rim U-shaped ring.
    // Keep the bottom-middle clear for the live numeric readout and the
    // side positions clear for the fixed 10/20 scale labels.
    constexpr float cx = 233.f;
    constexpr float half_width = 159.f; // band spans x=74..392
    constexpr float centre_y = 405.f;
    constexpr float rise = 65.f;        // ends at y=340, bottom at 405
    constexpr float tile_half_height = 12.5f;
    constexpr float tile_pitch = 2.f * half_width / 30.f;

    for (int y = 0; y < sweep_h; ++y) {
        const float py = static_cast<float>(y + sweep_y);
        for (int x = 0; x < sweep_w; ++x) {
            const float s = (static_cast<float>(x) - cx) / half_width;
            if (s < -1.04f || s > 1.04f) continue;
            const float abs_s = std::fabs(s);
            const float limited_s = abs_s > 1.f ? 1.f : abs_s;
            const float crown = std::pow(limited_s, 1.65f);
            const float mid_y = centre_y - rise * crown;
            const float derivative = abs_s < 0.0001f ? 0.f :
                -(rise * 1.65f / half_width) *
                (s > 0.f ? 1.f : -1.f) * std::pow(limited_s, 0.65f);
            const float v = py - mid_y;
            const float abs_v = std::fabs(v);
            // Normal-directed boundaries tilt the tiles with the curve:
            // vertical at centre, leaning progressively at either end.
            const float projected_x = static_cast<float>(x) +
                                      derivative * v;
            const float along = (projected_x - (cx - half_width)) / tile_pitch;
            const float seg_index = std::floor(along);
            const float fraction = along - seg_index;
            const bool inside_segment = seg_index >= 0.f && seg_index < 30.f;
            const float edge_distance = (fraction < 0.5f ?
                                        fraction : 1.f - fraction) * tile_pitch;
            // Consistent ~2px black gaps, with antialiased edges.
            const float gap_mask = clamp01((edge_distance - 1.0f) / 1.15f);
            const float vertical_mask = clamp01(
                (tile_half_height - abs_v) / 1.5f);
            const float tile = inside_segment ? gap_mask * vertical_mask : 0.f;

            // Cobalt bloom, slim upper/lower guide lines and lit tile edges.
            // These share the same curve so rails never drift from the bar.
            float base = 42.f * glow(abs_v - 14.3f, 1.35f);
            base += 15.f * glow(abs_v - 17.4f, 3.0f);
            base += 18.f * glow(abs_v - 11.5f, 5.3f);
            base += tile * (35.f + 25.f * glow(abs_v - 10.f, 2.8f));

            // Bright blue-to-ice centre, softly stronger towards mid-height.
            // Unlike the old raster, the two-color fill looks lit, not flat.
            const float bright = tile * (187.f + 54.f * glow(v, 6.8f));
            const float end_fade = clamp01((1.035f - abs_s) / 0.035f);
            const int pos = y * sweep_w + x;
            blue[pos] = alpha_clamp(base * end_fade);
            ice[pos] = alpha_clamp(bright * end_fade);
        }
    }
}

inline void rasterize() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    if (allocate_image(road)) paint_road();
    const bool blue_ready = allocate_image(afr_sweep);
    const bool tile_ready = allocate_image(afr_tile_light);
    if (blue_ready && tile_ready) paint_afr_sweep();
}
} // namespace cockpit_art
