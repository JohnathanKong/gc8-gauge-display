#pragma once

// Test protocol: GC8|1|RIGHT|250 m|Main St (ASCII, max 100 bytes).
// This transport is diagnostic-only; never vehicle control.
struct Gc8BleSnapshot {
    bool connected;
    bool has_navigation;
    char maneuver[16];
    char distance[20];
    char road[48];
    char destination[48];
    bool has_demo_afr;
    char demo_afr[8];
};

void gc8_ble_start();
Gc8BleSnapshot gc8_ble_snapshot();
