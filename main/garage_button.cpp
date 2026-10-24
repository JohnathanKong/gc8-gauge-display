#include "garage_button.h"
#include "gc8_ble.h"

#include <cstdint>
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char TAG[] = "gc8_garage_button";
constexpr uint16_t kExpanderAddress = 0x20;  // TCA9554
constexpr uint8_t kInputPortReg = 0x00;      // Read ONLY: never change BSP I/O output state.
constexpr uint8_t kPwrMask = 1u << 4;       // SYS_OUT HIGH while upper PWR is held.
constexpr int64_t kMinPressUs = 75000;
constexpr int64_t kMaxPressUs = 1200000;    // Long press belongs to AXP2101 power logic.
constexpr int64_t kCooldownUs = 6000000;    // Prevent inadvertent double-toggle.
constexpr int kSampleMs = 35;

i2c_master_dev_handle_t expander = nullptr;

bool read_upper_button(bool &pressed) {
    uint8_t reg = kInputPortReg, port = 0;
    if (!expander ||
        i2c_master_transmit_receive(expander, &reg, 1, &port, 1, 100) != ESP_OK)
        return false;
    pressed = (port & kPwrMask) != 0;
    return true;
}

void poll_task(void *) {
    // Share the existing I2C bus with the display, AXP2101 and touch.
    // We add only a read-only device handle for the TCA9554.
    auto *bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGW(TAG, "No I2C bus; garage button disabled");
        vTaskDelete(nullptr);
        return;
    }
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = kExpanderAddress;
    config.scl_speed_hz = 100000;
    const esp_err_t err = i2c_master_bus_add_device(bus, &config, &expander);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not register PWR button expander: %s",
                 esp_err_to_name(err));
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
