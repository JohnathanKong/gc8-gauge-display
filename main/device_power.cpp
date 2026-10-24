#include "device_power.h"

#include <atomic>
#include <cstdint>
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char TAG[] = "gc8_device_power";
constexpr uint16_t kAxp2101Address = 0x34;
constexpr uint8_t kChipIdRegister = 0x03;
constexpr uint8_t kExpectedChipId = 0x4A;
constexpr uint8_t kAlternateChipId = 0x47; // AXP2101 silicon variants
constexpr uint8_t kPmuStatusRegister = 0x00;
constexpr uint8_t kChargeStatusRegister = 0x01;
constexpr uint8_t kFuelGaugePercentRegister = 0xA4;
constexpr uint8_t kBatteryPresentMask = 0x08;
constexpr uint8_t kVbusGoodMask = 0x20;
constexpr char kNvsNamespace[] = "gc8_display";
constexpr char kBrightnessKey[] = "bright";
constexpr char kIdleSecondsKey[] = "idle_s";

i2c_master_dev_handle_t pmic = nullptr;
// All values are sampled independently. A board without a battery can still
// report USB power; do not call it "charging" just because USB is connected.
std::atomic<int> cached_percent{-1};
std::atomic<bool> cached_charging{false};
std::atomic<bool> cached_status_valid{false};
std::atomic<bool> cached_vbus_good{false};
std::atomic<bool> cached_battery_present{false};
std::atomic<uint8_t> normal_brightness{75};
std::atomic<uint16_t> idle_dim_seconds{300};
// 32-bit monotonic milliseconds are native atomics on ESP32-S3; subtraction
// remains wrap-safe across the 49-day uptime rollover.
std::atomic<uint32_t> last_interaction_ms{0};

bool read_axp_register(uint8_t reg, uint8_t &value) {
    return pmic &&
           i2c_master_transmit_receive(pmic, &reg, 1, &value, 1, 100) == ESP_OK;
}

void read_settings() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t bright = 75;
    uint16_t idle = 300;
    if (nvs_get_u8(handle, kBrightnessKey, &bright) == ESP_OK &&
        (bright == 40 || bright == 60 || bright == 75 ||
         bright == 90 || bright == 100)) normal_brightness.store(bright);
    if (nvs_get_u16(handle, kIdleSecondsKey, &idle) == ESP_OK &&
        (idle == 0 || idle == 60 || idle == 300 || idle == 900))
        idle_dim_seconds.store(idle);
    nvs_close(handle);
}

void persist_settings() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable for power preferences");
        return;
    }
    esp_err_t err = nvs_set_u8(handle, kBrightnessKey, normal_brightness.load());
    if (err == ESP_OK)
        err = nvs_set_u16(handle, kIdleSecondsKey, idle_dim_seconds.load());
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "Display preference save failed: %s", esp_err_to_name(err));
    nvs_close(handle);
}

void sample_power() {
    uint8_t status = 0;
    if (!read_axp_register(kPmuStatusRegister, status)) {
        cached_status_valid.store(false);
        cached_percent.store(-1);
        cached_charging.store(false);
        cached_vbus_good.store(false);
        cached_battery_present.store(false);
        return;
    }
    const bool battery = (status & kBatteryPresentMask) != 0;
    const bool vbus = (status & kVbusGoodMask) != 0;
    cached_status_valid.store(true);
    cached_vbus_good.store(vbus);
    cached_battery_present.store(battery);

    uint8_t percentage = 0;
    if (!battery || !read_axp_register(kFuelGaugePercentRegister, percentage) ||
        percentage > 100) {
        cached_percent.store(-1);
    } else {
        cached_percent.store(percentage);
    }
    uint8_t charge_status = 0;
    const bool charge_valid = battery &&
        read_axp_register(kChargeStatusRegister, charge_status);
    cached_charging.store(charge_valid && ((charge_status >> 5) & 0x03) == 1);
}

void monitor_task(void *) {
    // The BSP owns this bus, also shared with touch and the QMI8658 IMU.
    // Never initialize a second I2C controller or change PMIC power rails.
    auto *bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGW(TAG, "BSP I2C unavailable; battery/USB status unknown");
        vTaskDelete(nullptr);
        return;
    }
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = kAxp2101Address;
    cfg.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(bus, &cfg, &pmic) != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 unavailable; showing unknown power source");
        vTaskDelete(nullptr);
        return;
    }
    uint8_t chip_id = 0;
    if (!read_axp_register(kChipIdRegister, chip_id) ||
        (chip_id != kExpectedChipId && chip_id != kAlternateChipId)) {
        ESP_LOGW(TAG, "Unexpected AXP2101 ID 0x%02X; status unknown", chip_id);
        i2c_master_bus_rm_device(pmic);
        pmic = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "AXP2101 ready (VBUS, battery presence, charge state)");
    int old_percent = -2;
    int old_vbus = -1;
    int old_battery = -1;
    for (;;) {
        sample_power();
        const int pct = cached_percent.load();
        const int usb = cached_status_valid.load() ?
                        (cached_vbus_good.load() ? 1 : 0) : -1;
        const int batt = cached_status_valid.load() ?
                         (cached_battery_present.load() ? 1 : 0) : -1;
        if (pct != old_percent || usb != old_vbus || batt != old_battery) {
            ESP_LOGI(TAG, "Power: VBUS=%d battery=%d gauge=%d%% charging=%d",
                     usb, batt, pct, static_cast<int>(cached_charging.load()));
            old_percent = pct;
            old_vbus = usb;
            old_battery = batt;
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // Catch cable plug/unplug promptly.
    }
}
} // namespace

void device_power_start() {
    read_settings(); // Caller initializes NVS before entering here.
    last_interaction_ms.store(static_cast<uint32_t>(esp_timer_get_time() / 1000));
    if (xTaskCreate(monitor_task, "device_battery", 3072, nullptr, 3, nullptr) != pdPASS)
        ESP_LOGW(TAG, "Power monitor unavailable; brightness preference remains");
}

DevicePowerSnapshot device_power_snapshot() {
    const int percent = cached_percent.load();
    return {percent >= 0 && percent <= 100,
            static_cast<uint8_t>(percent >= 0 && percent <= 100 ? percent : 0),
            cached_charging.load(), cached_status_valid.load(),
            cached_vbus_good.load(), cached_battery_present.load()};
}

void device_power_note_activity() {
    last_interaction_ms.store(static_cast<uint32_t>(esp_timer_get_time() / 1000));
}

uint8_t device_power_target_brightness(bool route_active, bool diagnostics_open) {
    const uint8_t bright = normal_brightness.load();
    const uint16_t idle = idle_dim_seconds.load();
    if (route_active || diagnostics_open || idle == 0) return bright;
    const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    const uint32_t elapsed_ms = now_ms - last_interaction_ms.load();
    if (elapsed_ms >= static_cast<uint32_t>(idle) * 1000U) {
        // Real panel dimming, NOT ESP32 deep sleep; BLE/navigation/garage stay up.
        return 25;
    }
    return bright;
}

uint8_t device_power_user_brightness() {
    return normal_brightness.load();
}

uint16_t device_power_idle_dim_seconds() {
    return idle_dim_seconds.load();
}

uint8_t device_power_cycle_brightness() {
    constexpr uint8_t levels[] = {40, 60, 75, 90, 100};
    const uint8_t current = normal_brightness.load();
    uint8_t next = levels[0];
    for (unsigned i = 0; i < sizeof(levels); ++i) {
        if (levels[i] == current) {
            next = levels[(i + 1) % sizeof(levels)];
            break;
        }
    }
    normal_brightness.store(next);
    device_power_note_activity();
    persist_settings();
    return next;
}

uint16_t device_power_cycle_idle_dim() {
    constexpr uint16_t periods[] = {0, 60, 300, 900};
    const uint16_t current = idle_dim_seconds.load();
    uint16_t next = periods[0];
    for (unsigned i = 0; i < sizeof(periods)/sizeof(periods[0]); ++i) {
        if (periods[i] == current) {
            next = periods[(i + 1) % (sizeof(periods)/sizeof(periods[0]))];
            break;
        }
    }
    idle_dim_seconds.store(next);
    device_power_note_activity();
    persist_settings();
    return next;
}
