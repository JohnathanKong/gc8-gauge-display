# GC8 Gauge Display

Purpose-built firmware for the **Waveshare ESP32-S3-Touch-AMOLED-1.75C Rev 1.1**. This board runs our dashboard directly; there is no Android/Linux app launcher. A separate hidden controller will eventually send real telemetry.

## Current state

The initial LVGL dashboard source is committed. It shows GC8 branding, coolant, AFR, boost, RPM and an offline/controller status. Sensor values deliberately remain unavailable until connected to a real controller.

**Owner previously confirmed successful build, flash and operation of the LVGL dashboard on the physical Waveshare.** The developer uses ESP-IDF **v6.1**. The BLE integration has not been built or flashed.

## Windows 11 / VS Code — everyday build

Prerequisites: VS Code with the **Espressif IDF** extension configured, ESP-IDF installed, and this repository cloned. The development board uses ESP32-S3; the other board is kept as a factory reference.

1. Open this folder in VS Code: `C:\Users\User\Projects\GC8-Gauge\gc8-gauge-display`.
2. Press **Ctrl+Shift+P** and run **ESP-IDF: Open ESP-IDF Terminal**. **Do not use the ordinary PowerShell or Git Bash terminal**: they will show `ERROR: ESP-IDF environment is not active`.
3. In the newly opened ESP-IDF terminal, run **`build.bat`** (or `.\build.bat` in PowerShell).
4. Wait for the build to finish. **Do not flash if the build fails.** Copy the first actual error and the last 20–30 output lines for troubleshooting.

After a successful build, use the ESP-IDF extension's **Flash Your Project** command with the development board on the correct COM port. Then run **Monitor Device** to inspect boot logs. Exit the monitor with **Ctrl+]**. Flashing replaces the existing firmware; never flash the untouched reference board.

### First build after switching from the original generic starter

The old `sdkconfig` was generated for a different board configuration. Do this **only once**, before rebuilding the new dashboard:

1. Stop any running build or serial monitor.
2. In the project root, rename `sdkconfig` to `sdkconfig.backup` if it exists.
3. Delete the generated `build` directory if it exists.
4. Open the ESP-IDF terminal as above and run `build.bat`. The script selects `esp32s3` if no `sdkconfig` exists and generates a fresh config using `sdkconfig.defaults`.

**If a build is already running, let it finish.** Do not delete `sdkconfig` or `build` while it is building. If it succeeds, inspect the configuration before flashing; if it fails, collect the error first.

### Troubleshooting

- **`ESP-IDF environment is not active`**: Open **ESP-IDF: Open ESP-IDF Terminal** from the VS Code command palette. Reopening VS Code may require opening that terminal again.
- **`idf.py` not recognized**: Same cause: ESP-IDF environment not activated.
- **Component download or version conflict**: ESP-IDF v6.1 may be incompatible with the vendor BSP. Check the actual error before switching versions.
- **Screen still shows the factory image**: A prior generic program did not initialize the display. This is not evidence of a crash. Check serial logs and board support.
- **PSRAM/flash boot errors**: Verify board-specific `sdkconfig.defaults` were applied by a fresh configuration before trying another flash.

## Repository structure

- `main/main.cpp`: GC8 LVGL dashboard
- `main/idf_component.yml`: board-support and LVGL dependencies
- `sdkconfig.defaults`: initial board configuration
- `partitions.csv`: flash partition layout
- `build.bat`: Windows build helper (requires active ESP-IDF terminal)
- `.gitignore`: generated/local files excluded from Git

Board reference: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C

## Next milestones

1. Get a verified successful build and show the dashboard on the actual AMOLED.
2. Confirm LVGL layout and display performance.
3. Implement a small, versioned BLE telemetry protocol from the hidden controller.
4. Add real ECU/sensor data only after acquisition hardware is validated. Keep ECU communication read-only.

## Android BLE integration — started 2026-10-21

Android companion repository `JohnathanKong/gc8-gauge-display-android` now has an **unverified** BLE central prototype that scans for service UUID `a8e31c40-6b8f-4b67-a4f0-4a0b75000001` and discovers characteristic `a8e31c40-6b8f-4b67-a4f0-4a0b75000002`. These UUIDs are provisional protocol identifiers. **BLE peripheral source is now committed**, including NimBLE startup, advertising, GATT service, and a write endpoint that only logs payload length. **It has NOT been compiled or flashed yet, and end-to-end connection is NOT verified.** Next implement the ESP-IDF BLE peripheral with matching UUIDs, verify its build, then test connection with the Android APK on actual hardware. Keep the existing LVGL dashboard functional and avoid overwriting the untouched factory-reference board. The current Android prototype has no navigation data write path. Owner confirmed Samsung Fold8 Google Maps notification text and Samsung-specific candidate distance fields; movement semantics remain unverified.

### BLE peripheral source committed — 2026-10-21

Files: `main/gc8_ble.cpp`, `main/gc8_ble.h`; `main.cpp`, `main/CMakeLists.txt`, `sdkconfig.defaults` updated. Uses ESP-IDF NimBLE peripheral (no Classic Bluetooth), advertises name `GC8 Display` and service `a8e31c40-6b8f-4b67-a4f0-4a0b75000001`; exposes writable GATT characteristic `a8e31c40-6b8f-4b67-a4f0-4a0b75000002`. Incoming writes are length-checked and logged **without parsing or displaying them**. This is a connection smoke test, **not navigation rendering**. Existing LVGL UI remains unchanged. **No successful build or physical flash of this change has been reported.** First next action: pull the firmware repo and run `build.bat` inside the VS Code **ESP-IDF terminal**. Capture first build error if any; do not flash a failed build. `sdkconfig.defaults` changes may not apply to an already generated local `sdkconfig`; check `CONFIG_BT_ENABLED` and `CONFIG_BT_NIMBLE_ENABLED` if configuration seems missing, using `idf.py menuconfig` or carefully regenerating configuration. Once build succeeds, flash **only the development Waveshare**, not the factory reference board. Then rebuild Android and test BLE discovery. Security: initial write characteristic is unencrypted and unauthenticated; do not send sensitive data or vehicle commands. Before real use, add pairing/security and a versioned bounded payload parser.
