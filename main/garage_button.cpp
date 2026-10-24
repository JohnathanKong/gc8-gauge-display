#include "garage_button.h"
#include "gc8_ble.h"

#include <cstdint>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char TAG[] = "gc8_garage_button";
// Exact Waveshare ESP32-S3-Touch-AMOLED-1.75C (round 466x466):
// conditioned PWR sense SYS_OUT -> GPIO3, HIGH only when PWR is pressed.
// IMPORTANT: non-C 1.75 uses TCA9554 EXIO4 instead. Never use it here.
// GPIO0 is the LOWER BOOT button, reserved for flashing.
constexpr gpio_num_t kPwrSensePin = GPIO_NUM_3;
constexpr int64_t kMinPressUs = 75000;
constexpr int64_t kMaxPressUs = 1200000;     // Long press: AXP2101 power-off.
constexpr int64_t kCooldownUs = 6000000;     // Protect against duplicate actions.
constexpr int kSampleMs = 35;

void poll_task(void *) {
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << static_cast<uint32_t>(kPwrSensePin);
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK) {
        ESP_LOGE(TAG, "PWR GPIO3 not available; garage button disabled");
        vTaskDelete(nullptr);
        return;
    }

    bool stable = false, raw_prev = false, initialized = false;
    int same_readings = 0;
    int64_t pressed_at = 0, last_accepted_at = -kCooldownUs;
    ESP_LOGI(TAG, "Monitoring upper PWR on GPIO3 (HIGH=pressed); BOOT untouched");
    for (;;) {
        const bool raw = gpio_get_level(kPwrSensePin) != 0;
        if (raw == raw_prev) ++same_readings;
        else { raw_prev = raw; same_readings = 1; }

        // Three stable samples (~105ms) reject noise, bounce and quick spikes.
        if (same_readings >= 3 && (!initialized || raw != stable)) {
            const int64_t now = esp_timer_get_time();
            if (!initialized) {
                initialized = true;
                stable = raw;
                // Do not emit on a press already held during boot.
                pressed_at = 0;
            } else {
                stable = raw;
                if (stable) pressed_at = now;
                else if (pressed_at != 0) {
                    const int64_t duration = now - pressed_at;
                    pressed_at = 0;
                    if (duration >= kMinPressUs && duration <= kMaxPressUs &&
                        now - last_accepted_at >= kCooldownUs) {
                        last_accepted_at = now;
                        ESP_LOGI(TAG, "Upper PWR tapped; requesting garage routine");
                        gc8_ble_garage_button_pressed();
                    } else if (duration > kMaxPressUs) {
                        ESP_LOGI(TAG, "PWR held; no garage request");
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kSampleMs));
    }
}
} // namespace

void garage_button_start() {
    if (xTaskCreate(poll_task, "garage_pwr", 3072, nullptr, 3, nullptr) != pdPASS)
        ESP_LOGE(TAG, "Unable to start PWR GPIO3 reader");
}
