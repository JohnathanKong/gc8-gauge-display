#pragma once
#include <cstdint>

// Test protocol: GC8|1|RIGHT|250 m|Main St (ASCII, max 100 bytes).
// This transport is diagnostic-only; never vehicle control.
struct Gc8BleSnapshot {
    bool connected;
    bool has_navigation;
    bool route_active;  // START, DEST or valid turn; independent of distance
    char maneuver[16];
    char distance[20];
    char road[48];
    char destination[48];
    bool has_demo_afr;
    char demo_afr[8];

    // Device time comes from the phone. ESP32 extrapolates while disconnected
    // using its monotonic timer, and re-syncs on BLE reconnect/each minute.
    bool has_clock;
    bool clock_24h;
    int32_t clock_seconds; // Seconds after midnight in phone's local timezone
    int64_t clock_synced_us; // esp_timer_get_time() when packet was received
};

void gc8_ble_start();
Gc8BleSnapshot gc8_ble_snapshot();
