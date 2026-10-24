#include "device_power.h"

#include <atomic>
#include <cstdint>
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char TAG[] = "gc8_device_power";
constexpr uint16_t kAxp2101Address = 0x34;
constexpr uint8_t kChipIdRegister = 0x03;
constexpr uint8_t kExpectedChipId = 0x4A;
constexpr uint8_t kPmuStatusRegister = 0x00;
constexpr uint8_t kChargeStatusRegister = 0x01;
constexpr uint8_t kFuelGaugePercentRegister = 0xA4;
constexpr uint8_t kBatteryPresentMask = 0x08;

i2c_master_dev_handle_t pmic = nullptr;
// -1 means unknown, disconnected, absent or failed read. Never display a
// fabricated percentage when no cell is installed.
std::atomic<int> cached_percent{-1};
std::atomic<bool> cached_charging{false};

bool read_axp_register(uint8_t reg, uint8_t &value) {
    if (!pmic) return false;
    return i2c_master_transmit_receive(pmic, &reg, 1, &value, 1, 100) == ESP_OK;
}

bool sample_battery() {
    uint8_t status = 0;
    uint8_t percent = 0;
    if (!read_axp_register(kPmuStatusRegister, status) ||
        !(status & kBatteryPresentMask) ||
        !read_axp_register(kFuelGaugePercentRegister, percent) ||
        percent > 100) {
        cached_percent.store(-1, std::memory_order_relaxed);
        cached_charging.store(false, std::memory_order_relaxed);
        return false;
    }

    uint8_t charge_status = 0;
    const bool charge_valid = read_axp_register(kChargeStatusRegister, charge_status);
    // AXP2101 PMU status 2 bits 6:5: 01 = charging.
    const bool charging = charge_valid && ((charge_status >> 5) & 0x03) == 1;
    cached_charging.store(charging, std::memory_order_relaxed);
    cached_percent.store(percent, std::memory_order_relaxed);
    return true;
}

void monitor_task(void *) {
    // BSP owns the I2C controller shared with touch and audio. Add only a
    // device handle; NEVER create a second bus or write PMIC power registers.
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGW(TAG, "BSP I2C bus unavailable; hiding device battery");
        vTaskDelete(nullptr);
        return;
    }
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = kAxp2101Address;
    config.scl_speed_hz = 100000;  // conservative, shared bus
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &pmic);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 I2C device unavailable: %s", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }

    uint8_t chip_id = 0;
    if (!read_axp_register(kChipIdRegister, chip_id) ||
        chip_id != kExpectedChipId) {
        ESP_LOGW(TAG, "AXP2101 not detected (ID=0x%02X). Battery hidden.", chip_id);
        i2c_master_bus_rm_device(pmic);
        pmic = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "AXP2101 detected; polling real device battery");
    int previous_percent = -2;
    for (;;) {
        const bool available = sample_battery();
        const int current = cached_percent.load(std::memory_order_relaxed);
        if (current != previous_percent) {
            if (available) {
                ESP_LOGI(TAG, "Device battery: %d%%", current);
            } else {
                ESP_LOGI(TAG, "No valid battery reading; hiding battery icon");
            }
            previous_percent = current;
        }
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}
} // namespace

void device_power_start() {
    BaseType_t created = xTaskCreate(monitor_task, "device_battery",
                                     3072, nullptr, 3, nullptr);
    if (created != pdPASS) {
        ESP_LOGW(TAG, "Battery monitor task could not be started");
    }
}

DevicePowerSnapshot device_power_snapshot() {
    const int percent = cached_percent.load(std::memory_order_relaxed);
    return {percent >= 0 && percent <= 100,
            static_cast<uint8_t>(percent >= 0 && percent <= 100 ? percent : 0),
            cached_charging.load(std::memory_order_relaxed)};
}
