# GC8 Gauge Display

Custom single-purpose ESP-IDF firmware for Waveshare ESP32-S3-Touch-AMOLED-1.75C Rev 1.1.

## v0.1
Uses the Waveshare BSP and LVGL to render a GC8 dashboard with coolant, AFR, boost and RPM placeholders. No fake sensor readings, Bluetooth, or factory launcher.

## Build
Vendor examples are validated on ESP-IDF v6.0.2 / v5.5.5. Installed v6.1 is unverified. Dependencies download via ESP-IDF Component Manager.

**Before building:** Back up then remove the existing locally generated `sdkconfig` and `build/` directory, so `sdkconfig.defaults` applies the correct 32 MB flash, octal PSRAM, and partition settings. Select ESP32-S3 and build. Flash only the development board, not the untouched factory-reference unit.

Source based on official board support: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C

**Status:** Source committed; compilation and display operation still require local validation.
