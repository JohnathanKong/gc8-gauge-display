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

    // The reference is a CIRCULAR annular sweep. True polar geometry keeps
    // every upper/lower guide line concentric and every divider radial.
    // This replaces the previous polynomial parabola and slanted tiles.
    constexpr float pi = 3.14159265358979323846f;
    constexpr float first_angle = 46.f * pi / 180.f;
    constexpr float last_angle = 134.f * pi / 180.f;
    constexpr float total_angle = last_angle - first_angle;
    constexpr float cx = 233.f, cy = 210.f;
    constexpr float tile_inner = 189.f, tile_outer = 214.f;
    constexpr float rail_inner = 185.5f, rail_outer = 217.5f;
    constexpr int segments = 30;

    for (int y = 0; y < sweep_h; ++y) {
        const float dy = static_cast<float>(y + sweep_y) - cy;
        for (int x = 0; x < sweep_w; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float radius = std::sqrt(dx * dx + dy * dy);
            if (radius < 179.f || radius > 230.f) continue;
            const float angle = std::atan2(dy, dx);
            // Allow the continuous rails to extend slightly past the tiles.
            if (angle < first_angle - 0.013f ||
                angle > last_angle + 0.013f) continue;
            const bool in_tiles = angle >= first_angle && angle <= last_angle;
            const float position = (angle - first_angle) /
                                   total_angle * segments;
            const float segment_index = std::floor(position);
            const float fraction = position - segment_index;
            const float arc_pitch = radius * total_angle / segments;
            const float edge_distance = (fraction < 0.5f ? fraction :
                                         1.f - fraction) * arc_pitch;
            // Antialiased black radial gaps, not stretched parallelograms.
            const float gap_mask = clamp01((edge_distance - 1.0f) / 0.8f);
            const float tile_mask = in_tiles && segment_index >= 0.f &&
                                    segment_index < segments
                ? gap_mask * clamp01((radius - tile_inner) / 1.f) *
                             clamp01((tile_outer - radius) / 1.f)
                : 0.f;

            // Two continuous bright concentric lines, with a subdued outer
            // echo and cobalt bloom. These extend uninterrupted across gaps.
            const float base =
                18.f * glow(radius - 203.f, 17.f) +
                74.f * glow(radius - rail_inner, 2.3f) +
                108.f * glow(radius - rail_inner, 0.7f) +
                87.f * glow(radius - rail_outer, 2.6f) +
                140.f * glow(radius - rail_outer, 0.8f) +
                14.f * glow(radius - 225.f, 2.8f);
            const float tiles = tile_mask *
                (30.f + 24.f * glow(radius - 190.f, 2.3f) +
                        22.f * glow(radius - 213.f, 2.3f));
            const float tile_glow = tile_mask *
                (205.f + 28.f * glow(radius - 202.f, 7.f));
            // Icy-white pinstripes distinguish the thin reference rails
            // from the richer blue segment bodies and their dark dividers.
            const float icy_rails =
                98.f * glow(radius - rail_inner, 0.78f) +
                148.f * glow(radius - rail_outer, 0.84f);
            const int pixel = y * sweep_w + x;
            blue[pixel] = alpha_clamp(base + tiles);
            ice[pixel] = alpha_clamp(tile_glow + icy_rails);
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
