#pragma once
#include <cstdint>

// Battery is the WAVE SHARE DEVICE battery (AXP2101 fuel gauge), not phone,
// vehicle, or an estimated voltage. Unknown/absent batteries are not displayed.
struct DevicePowerSnapshot {
    bool valid;
    uint8_t percent;
    bool charging;
};

// Start the periodic power monitor AFTER bsp_display_start(), when the
// Waveshare BSP's shared I2C bus has been initialized.
void device_power_start();
DevicePowerSnapshot device_power_snapshot();
