#pragma once
#include <cstdint>

// Measurements are from the Waveshare's AXP2101 (not the phone).
// Source detection is independent of an installed battery.
struct DevicePowerSnapshot {
    bool valid;            // Valid battery fuel gauge percentage
    uint8_t percent;
    bool charging;
    bool vbus_valid;       // PMIC status register was readable
    bool external_power;   // AXP2101 STATUS1 bit 5: VBUS good
    bool battery_present;  // AXP2101 STATUS1 bit 3
};

void device_power_start();
DevicePowerSnapshot device_power_snapshot();

// Display brightness only. Never shut down the ESP32 or BLE while idling.
void device_power_note_activity();
uint8_t device_power_target_brightness(bool route_active, bool diagnostics_open);

// Saved under NVS namespace gc8_display, at boot after NVS initialization.
// Brightness cycles 40/60/75/90/100%; idle dim cycles OFF/1/5/15 minutes.
uint8_t device_power_user_brightness();
uint16_t device_power_idle_dim_seconds();
uint8_t device_power_cycle_brightness();
uint16_t device_power_cycle_idle_dim();
