#include "device_orientation.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

namespace {
constexpr char TAG[] = "gc8_orientation";
// Exact Waveshare ESP32-S3-Touch-AMOLED-1.75C (QMI8658), shared BSP I2C.
constexpr uint8_t kImuAddress = 0x6B;
constexpr uint8_t kWhoAmIReg = 0x00;
constexpr uint8_t kWhoAmIValue = 0x05;
constexpr uint8_t kCtrl1Reg = 0x02;
constexpr uint8_t kCtrl2Reg = 0x03;
constexpr uint8_t kCtrl7Reg = 0x08;
constexpr uint8_t kAccelDataReg = 0x35;
constexpr uint8_t kStatus0Reg = 0x2E;
constexpr char kNvsNamespace[] = "gc8_orient";
constexpr char kNvsMode[] = "mode";
constexpr char kNvsRotation[] = "rotation";
constexpr char kNvsZeroSector[] = "reference";

// 0=manual, 1=auto. Default is manual 0 degrees until explicitly calibrated.
// A fixed portrait reference is more reliable than guessing how the QMI8658
// chip is soldered relative to this exact Waveshare enclosure.
std::atomic<uint8_t> rotation{0};
std::atomic<uint8_t> auto_mode{0};
std::atomic<int> zero_sector{-1};
std::atomic<int> stable_sector{-1};
i2c_master_dev_handle_t imu_device = nullptr;
bool started = false;

void persist_settings() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Orientation persistence unavailable");
        return;
    }
    esp_err_t result = nvs_set_u8(handle, kNvsMode, auto_mode.load());
    if (result == ESP_OK) result = nvs_set_u8(handle, kNvsRotation, rotation.load());
    if (result == ESP_OK) {
        const int reference = zero_sector.load();
        result = nvs_set_u8(handle, kNvsZeroSector,
                            reference >= 0 && reference < 4
                                ? static_cast<uint8_t>(reference) : 255);
    }
    if (result == ESP_OK) result = nvs_commit(handle);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Orientation save failed: %s", esp_err_to_name(result));
    }
    nvs_close(handle);
}

void load_settings() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t saved_rotation = 0, saved_mode = 0, saved_zero = 255;
    nvs_get_u8(handle, kNvsRotation, &saved_rotation);
    nvs_get_u8(handle, kNvsMode, &saved_mode);
    nvs_get_u8(handle, kNvsZeroSector, &saved_zero);
    nvs_close(handle);
    if (saved_rotation < 4) rotation.store(saved_rotation);
    if (saved_zero < 4) {
        zero_sector.store(saved_zero);
        if (saved_mode == 1) auto_mode.store(1);
    }
}

bool read_register(uint8_t reg, uint8_t &value) {
    return imu_device &&
           i2c_master_transmit_receive(imu_device, &reg, 1, &value, 1, 100) == ESP_OK;
}
bool write_register(uint8_t reg, uint8_t value) {
    const uint8_t payload[] = {reg, value};
    return imu_device &&
           i2c_master_transmit(imu_device, payload, sizeof(payload), 100) == ESP_OK;
}
bool read_acceleration(int16_t &ax, int16_t &ay, int16_t &az) {
    // The QMI8658's CTRL1 bit 6 enables register auto increment. Raw data
    // is little-endian when bit 5 is clear. 2g => 16384 counts per g.
    uint8_t reg = kAccelDataReg;
    uint8_t values[6] = {};
    uint8_t status = 0;
    if (!read_register(kStatus0Reg, status) || !(status & 0x01) ||
        i2c_master_transmit_receive(imu_device, &reg, 1, values,
                                    sizeof(values), 100) != ESP_OK) return false;
    auto signed16 = [](uint8_t low, uint8_t high) {
        return static_cast<int16_t>(static_cast<uint16_t>(low) |
                                     (static_cast<uint16_t>(high) << 8));
    };
    ax = signed16(values[0], values[1]);
    ay = signed16(values[2], values[3]);
    az = signed16(values[4], values[5]);
    return true;
}

// Sector is a direction of gravity in the IMU's plane, not absolute screen
// orientation. The user calibrates zero with USB below the upright screen.
// The difference between gravity sectors gives rotation without needing to
// assume which way the sensor chip is soldered on the PCB.
int gravity_sector(float x, float y, float z) {
    const float xy_sq = x*x + y*y;
    const float mag_sq = xy_sq + z*z;
    // Don't flip while lying flat, during strong motion, or near a diagonal.
    if (mag_sq < 11500.0f*11500.0f ||
        mag_sq > 22500.0f*22500.0f ||
        xy_sq < 11700.0f*11700.0f) return -1;
    const float ax = std::fabs(x), ay = std::fabs(y);
    if (ay > ax * 1.40f) return y >= 0 ? 0 : 2;
    if (ax > ay * 1.40f) return x >= 0 ? 1 : 3;
    return -1;
}

void monitor_task(void *) {
    const auto bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGW(TAG, "IMU bus unavailable; screen still rotates by tap");
        vTaskDelete(nullptr);
        return;
    }
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = kImuAddress;
    cfg.scl_speed_hz = 100000; // Shared with PMIC and capacitive touch.
    if (i2c_master_bus_add_device(bus, &cfg, &imu_device) != ESP_OK) {
        ESP_LOGW(TAG, "IMU I2C registration failed; manual rotation remains");
        vTaskDelete(nullptr);
        return;
    }
    uint8_t who = 0;
    // Preserve the original dashboard and I2C devices if sensor not found.
    if (!read_register(kWhoAmIReg, who) || who != kWhoAmIValue ||
        !write_register(kCtrl1Reg, 0x40) || // register auto-increment; LE
        !write_register(kCtrl2Reg, 0x07) || // ±2 g at 62.5 Hz
        !write_register(kCtrl7Reg, 0x01)) { // accelerometer only
        ESP_LOGW(TAG, "QMI8658 unavailable (WHO_AM_I=0x%02X); manual rotation remains", who);
        i2c_master_bus_rm_device(imu_device);
        imu_device = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "QMI8658 ready; calibrated auto-rotation=%d",
             static_cast<int>(auto_mode.load()));
    int candidate = -1, samples = 0;
    float fx = 0, fy = 0, fz = 0;
    bool filter_ready = false;
    for (;;) {
        int16_t x = 0, y = 0, z = 0;
        if (read_acceleration(x, y, z)) {
            if (!filter_ready) {
                fx = x; fy = y; fz = z;
                filter_ready = true;
            } else {
                // Damp vehicle vibration without accumulating arbitrary gyro
                // drift; gravity is enough for 4-way landscape/portrait.
                constexpr float alpha = 0.26f;
                fx += alpha * (x - fx);
                fy += alpha * (y - fy);
                fz += alpha * (z - fz);
            }
            const int sector = gravity_sector(fx, fy, fz);
            if (sector < 0) {
                candidate = -1;
                samples = 0;
                stable_sector.store(-1);
            } else if (sector != candidate) {
                candidate = sector;
                samples = 1;
                stable_sector.store(-1);
            } else {
                if (samples < 20) ++samples;
                if (samples >= 10) { // roughly 1.5 seconds stable
                    stable_sector.store(sector);
                    if (auto_mode.load() != 0) {
                        const int zero = zero_sector.load();
                        if (zero >= 0 && zero < 4) {
                            // Counter-rotate content to compensate for the
                            // physical device's quarter-turn from USB-down.
                            const uint8_t wanted =
                                static_cast<uint8_t>((sector - zero + 4) % 4);
                            if (wanted != rotation.load()) {
                                rotation.store(wanted);
                                ESP_LOGI(TAG, "Stable gravity sector=%d, rotation=%u",
                                         sector, static_cast<unsigned>(wanted * 90));
                            }
                        }
                    }
                }
            }
        } else {
            // Missing/old data must not cause a spontaneous rotation.
            candidate = -1;
            samples = 0;
            stable_sector.store(-1);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
} // namespace

void device_orientation_start() {
    if (started) return;
    started = true;
    load_settings();
    if (xTaskCreate(monitor_task, "gc8_imu", 3072, nullptr, 2, nullptr) != pdPASS)
        ESP_LOGW(TAG, "IMU task unavailable; screen remains manually rotatable");
}

uint8_t device_orientation_rotation() {
    return rotation.load();
}

bool device_orientation_is_auto() {
    return auto_mode.load() != 0;
}

uint8_t device_orientation_next_manual() {
    auto_mode.store(0);
    const uint8_t next = static_cast<uint8_t>((rotation.load() + 1) % 4);
    rotation.store(next);
    persist_settings();
    ESP_LOGI(TAG, "Manual screen rotation=%u degrees", static_cast<unsigned>(next * 90));
    return next;
}

bool device_orientation_calibrate_auto() {
    const int reference = stable_sector.load();
    if (reference < 0 || reference > 3) {
        ESP_LOGW(TAG, "Cannot calibrate: hold device vertical/still with USB below screen");
        return false;
    }
    zero_sector.store(reference);
    rotation.store(0); // USB-down physical upright reference.
    auto_mode.store(1);
    persist_settings();
    ESP_LOGI(TAG, "Calibrated USB-down gravity reference=%d; auto rotation enabled", reference);
    return true;
}
