#include "audio_reactivity.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "gc8_audio";
// Waveshare's ES7210 spectrum-analyzer example uses 16k/16-bit/stereo.
constexpr int kSampleRate = 16000;
constexpr int kChannels = 2;
constexpr int kFrames = 256;
std::atomic<bool> enabled{false};
std::atomic<uint16_t> level_1000{0};

void microphone_task(void *) {
    // BSP owns the shared I2C/I2S bus, ES7210 routing and codec lifecycle.
    // Do NOT install a second I2C driver on GPIO14/GPIO15.
    esp_codec_dev_handle_t mic = bsp_audio_codec_microphone_init();
    if (!mic) {
        ESP_LOGE(TAG, "ES7210 microphone init failed; idle line stays flat");
        vTaskDelete(nullptr);
        return;
    }

    esp_codec_dev_sample_info_t cfg = {};
    cfg.sample_rate = kSampleRate;
    cfg.channel = kChannels;
    cfg.bits_per_sample = 16;
    if (esp_codec_dev_open(mic, &cfg) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "ES7210 stream open failed; idle line stays flat");
        vTaskDelete(nullptr);
        return;
    }
    if (esp_codec_dev_set_in_gain(mic, 24.0f) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Could not set ES7210 input gain; using codec default");
    }

    int16_t samples[kFrames * kChannels] = {};
    unsigned read_failures = 0;
    while (true) {
        if (!enabled.load(std::memory_order_relaxed)) {
            level_1000.store(0, std::memory_order_relaxed);
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }
        const int result = esp_codec_dev_read(mic, samples, sizeof(samples));
        if (result != ESP_CODEC_DEV_OK) {
            level_1000.store(0, std::memory_order_relaxed);
            if (++read_failures == 5) {
                ESP_LOGW(TAG, "Microphone read failed repeatedly (%d)", result);
            }
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }
        read_failures = 0;

        // Remove DC offset before calculating RMS. Stereo interleaved input
        // is averaged; no actual audio samples leave this task.
        int64_t sum = 0;
        for (int i = 0; i < kFrames * kChannels; ++i) sum += samples[i];
        const float mean = static_cast<float>(sum) /
                           static_cast<float>(kFrames * kChannels);
        double sum_squares = 0.0;
        for (int i = 0; i < kFrames * kChannels; ++i) {
            const float centred = static_cast<float>(samples[i]) - mean;
            sum_squares += centred * centred;
        }
        const float rms = std::sqrt(static_cast<float>(
            sum_squares / static_cast<double>(kFrames * kChannels))) /
            32768.f;
        // Tunable decorative sensitivity: quiet cabin => near-flat baseline;
        // speech/exhaust/music/traffic push the line higher. Never RPM.
        const float amplitude = std::clamp((rms - 0.003f) * 20.f, 0.f, 1.f);
        level_1000.store(static_cast<uint16_t>(amplitude * 1000.f + 0.5f),
                         std::memory_order_relaxed);
    }
}
} // namespace

void audio_reactivity_start() {
    BaseType_t ok = xTaskCreate(microphone_task, "gc8_mic", 5 * 1024,
                                nullptr, 4, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Cannot start microphone task; idle line stays flat");
    }
}

void audio_reactivity_enable(bool on) {
    enabled.store(on, std::memory_order_relaxed);
    if (!on) level_1000.store(0, std::memory_order_relaxed);
}

float audio_reactivity_level() {
    return static_cast<float>(level_1000.load(std::memory_order_relaxed)) /
           1000.f;
}
