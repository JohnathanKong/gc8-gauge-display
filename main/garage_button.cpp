#include "garage_button.h"
#include "gc8_ble.h"

#include <cstdint>
#include "bsp/esp-bsp.h"
#include "esp_io_expander.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char TAG[] = "gc8_garage_button";
constexpr uint32_t kPwrMask = IO_EXPANDER_PIN_NUM_4; // SYS_OUT high while upper PWR held.
constexpr int64_t kMinPressUs = 75000;
constexpr int64_t kMaxPressUs = 1200000;    // Long press belongs to AXP2101 power logic.
constexpr int64_t kCooldownUs = 6000000;    // Prevent inadvertent double-toggle.
constexpr int kSampleMs = 35;

esp_io_expander_handle_t expander = nullptr;

bool read_upper_button(bool &pressed) {
    uint32_t levels = 0;
    if (!expander ||
        esp_io_expander_get_level(expander, kPwrMask, &levels) != ESP_OK)
        return false;
    pressed = (levels & kPwrMask) != 0;
    return true;
}

void poll_task(void *) {
    // Reuse the BSP's existing TCA9554 instance and shared I2C bus.
    // Adding a second independent device at 0x20 can conflict with the
    // audio/display expander driver, so do not create one.
    expander = bsp_io_expander_init();
    if (!expander) {
        ESP_LOGW(TAG, "BSP TCA9554 unavailable; garage button disabled");
        vTaskDelete(nullptr);
        return;
    }

    bool stable = false, raw_prev = false, initialized = false;
    int same_readings = 0;
    int64_t pressed_at = 0, last_accepted_at = -kCooldownUs;
    ESP_LOGI(TAG, "Upper PWR button monitor armed; lower BOOT is unused");

    for (;;) {
        bool raw = false;
        if (!read_upper_button(raw)) {
            same_readings = 0;
            vTaskDelay(pdMS_TO_TICKS(kSampleMs));
            continue;
        }

        if (raw == raw_prev) ++same_readings;
        else { raw_prev = raw; same_readings = 1; }

        // Need >= ~105 ms of stable reads to reject bounce and noise.
        if (same_readings >= 3 && (!initialized || raw != stable)) {
            const int64_t now = esp_timer_get_time();
            if (!initialized) {
                initialized = true;
                stable = raw;
                // A button already held during boot is never treated as an
                // intentional new garage command.
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
                        ESP_LOGI(TAG, "Upper PWR tapped; requesting garage action");
                        gc8_ble_garage_button_pressed();
                    } else if (duration > kMaxPressUs) {
                        ESP_LOGI(TAG, "Upper PWR held; ignoring garage action");
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kSampleMs));
    }
}
} // namespace

void garage_button_start() {
    if (xTaskCreate(poll_task, "garage_pwr", 3072, nullptr, 3, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Unable to start garage PWR reader");
    }
}
