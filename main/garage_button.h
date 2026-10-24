#pragma once
// Exact ESP32-S3-Touch-AMOLED-1.75C physical keys:
// upper PWR: GPIO3 (active HIGH), reserved for garage request;
// lower BOOT: GPIO0 (active LOW), configurable Android app shortcut.
// Holding BOOT during board reset still selects ESP32 ROM download mode.
// Only short presses after boot are handled as regular inputs.
void garage_button_start();
