#pragma once
#include <cstdint>

// Independent orientation controller. The display remains 466x466; rotation
// occurs in the panel's CO5300 hardware, not in individual LVGL widgets.
void device_orientation_start();
uint8_t device_orientation_rotation();  // 0, 1, 2, 3 = 0/90/180/270
bool device_orientation_is_auto();

// Short touch: rotate by one quarter-turn and lock that orientation.
// Persists the manual selection across resets.
uint8_t device_orientation_next_manual();

// Long touch while holding the physical device USB-port-down and vertical:
// record QMI8658 gravity sector as the upright reference and enable auto.
// Returns false if no valid stationary/vertical IMU sample is available.
bool device_orientation_calibrate_auto();
