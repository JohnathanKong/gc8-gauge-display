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

### First BLE firmware build attempt — 2026-10-21

Owner's ESP-IDF v6.1 Windows build failed compiling `main/gc8_ble.cpp` with `-Werror=missing-field-initializers` at line 44 (NimBLE GATT characteristic aggregate initialization). Commit `be076456` replaces partially initialized C aggregates with zero-initialized arrays and explicit field assignments before GATT registration. **Fix committed, rebuild not yet confirmed.** Next: `git pull`, then `build.bat` in ESP-IDF terminal; inspect any new compiler errors before flashing.

### BLE firmware build confirmed — 2026-10-21

Owner supplied ESP-IDF terminal screenshot showing successful ESP32-S3 image generation, `Project build complete`, and ~89% of app partition free after the C++ GATT initializer fix. Kconfig deprecation/default-value notes did not prevent build. **Build verified; BLE firmware flashing, advertising, and Android connectivity are still unverified.** Next flash only development board and inspect device log for `Advertising GC8 service` (or startup errors). Do not assume BLE is working merely because compilation succeeded.

### Development Waveshare flashed — owner confirmation, 2026-10-21

Owner confirmed flashing the newly built BLE firmware to the development Waveshare and seeing the existing dashboard with `WAITING FOR CONTROLLER`. This verifies firmware boot and LVGL rendering after the BLE changes; the label refers to the future ECU/sensor controller, **not BLE connection state**. BLE advertising, GATT connection, and data writes remain unverified. Next: build/install Android BLE client and press `Connect to GC8 display (BLE)` with the powered-on Waveshare nearby; if not found, inspect ESP-IDF serial monitor for `Advertising GC8 service` and Bluetooth startup errors.

### First real BLE connection confirmed — 2026-10-21

Owner supplied screenshot of the Galaxy Z Fold8 GC8 Companion displaying `BLE: GC8 BLE connected; write characteristic ready.` after tapping the BLE connection button while the development Waveshare was running the newly flashed firmware. This **verifies BLE service discovery, GATT connection, and write-characteristic discovery on real hardware**. It does **not** yet verify a successful characteristic write, a received message on ESP32, on-screen Bluetooth state, or navigation rendering. Screenshot also showed `No Google Maps notification captured yet` in that session, which is independent of BLE connectivity. Next milestone: implement and verify a harmless test write from Android, ESP32 reception, then on-screen indication.

### End-to-end BLE test message — source committed 2026-10-22 (NOT YET TESTED)

Both repositories now contain the first data-transfer test implementation. Android adds **Send test navigation to Waveshare**, requests BLE MTU 185 before GATT discovery, and writes `GC8|1|RIGHT|250 m|Main St` with write-response completion status. ESP32 accepts only a bounded, versioned, printable ASCII message with maneuver RIGHT/LEFT/STRAIGHT, stores a thread-safe snapshot, and refreshes LVGL labels: `PHONE: WAITING/CONNECTED`, `NAV: RIGHT 250 m`, and `Main St` when received; otherwise `NAV: NO DATA` / `ECU: OFFLINE`. The ESP32 is **not** extracting ECU telemetry or actual Google Maps navigation, and the Android write callback confirms GATT completion, not necessarily that a person observed the screen. **Both new code paths are committed but uncompiled/unflashed/uninstalled and not device-verified.** First build/flash ESP32 from ESP-IDF terminal (`git pull`, `build.bat`, flash development board only). Then build Android (`git pull`, `docker compose run --rm android gradle --no-daemon assembleDebug`), transfer APK via Google Drive, connect BLE, tap Send test navigation, and visually confirm screen. Stop and share compiler errors if either build fails. This test endpoint is unauthenticated and should not carry private navigation destinations or vehicle-control commands. Real navigation streaming, reconnect, authentication, and reliable device-state reporting remain future work.

### 2026-10-22 BLE data-transfer build evidence

Owner provided ESP-IDF terminal screenshot showing `Successfully created ESP32-S3 image` and `Project build complete` for the newly updated Waveshare firmware with BLE test navigation UI; ~89% of application partition remains free. **Compiled successfully; flashing this revision and visually verifying `PHONE: WAITING` / test navigation are still pending.** The Android data-write revision is not yet reported built or installed. Next flash development Waveshare and verify visible status, then install updated Android APK and test write.

### BLE navigation display firmware flashed — 2026-10-22

Owner confirmed the updated development Waveshare firmware was flashed and the new phone-waiting UI appeared as expected. This verifies physical boot and the new UI baseline; **a phone connection on this revision and test navigation data appearing on the screen have not yet been verified**. Next use the updated Android app: connect BLE, tap `Send test navigation to Waveshare`, and check the Waveshare for `PHONE: CONNECTED`, `NAV: RIGHT 250 m`, and `Main St`.

## Agreed product design direction — 2026-10-22

**Status: approved design direction, not yet implemented as the production UI.** The owner approved the circular, minimal, black-background navigation-first concept for the Waveshare ESP32-S3-Touch-AMOLED-1.75C. The sketch is inspiration, **not a literal layout or styling specification**.

- **Home/default page: Navigation.** Give the large next-maneuver arrow the strongest emphasis, followed by distance to maneuver and street name. Allocate roughly **70–75% of usable display space** to navigation; preserve legibility at a quick glance. Avoid thick decorative borders/rings that waste pixels. Start on this page at boot.
- **Persistent lower instrument: Wideband AFR.** Reserve the lower ~25–30% for a prominent numeric AFR value and a subtle coloured accent/arc/bar. AFR must remain visible **on every page**, including phone and vehicle pages. Display `--.-` when there is no verified sensor feed; never fabricate a live reading. Do not colour-code danger/normal states without validated sensor readings and context-aware thresholds (stoichiometric cruising versus richer boosted operation). Avoid flashing/changing the entire screen background with normal AFR fluctuations.
- **Horizontal page navigation:** From the home page, swipe **right for Phone**, swipe **left for Vehicle**. Phone page can prioritize favourites/recent calls; a full keypad is exploratory and may be unsuitable for the small display, especially while driving. Vehicle page will eventually show ECU/sensor information; prioritize useful metrics later. **Boost is explicitly deferred** rather than included in the initial home screen.
- **Incoming call UI:** A compact top-down overlay with answer/decline controls is the desired interaction; it must not permanently replace the selected page or hide critical warnings. Android call-control access and coexistence with the existing Bluetooth audio head unit remain **unverified technical dependencies**, not promised functionality.
- **Navigation source:** Google Maps on the Android phone supplies candidate instruction/distance data; the Waveshare displays it over BLE. Distance semantics and maneuver icon extraction still need validation. Maintain clear phone/ECU connection states. Do not imply the current BLE test message is live Google Maps guidance.
- **Future map view:** Investigate a miniature moving map **later**, only if technically feasible and useful at this display size. A map consumes valuable screen space; it must not displace the navigation-first experience or the persistent AFR reading. Treat as a separate design/technical investigation, **not part of the initial implementation or a commitment**.
- **Visual language:** High-contrast white navigation graphics and readable typography on deep black, restrained accents, natural circular composition, minimal chrome. The approved concept is directional guidance, not a pixel-perfect mockup.

**Implementation sequence:** first verify Android test-message write and ESP32 display on physical hardware; then implement the approved navigation-first layout with persistent AFR placeholder; then connect verified Google Maps updates; add real wideband hardware feed and call/vehicle features only after their respective integrations are validated. No final UI build or screenshot has yet been approved on hardware.

### Navigation-first screen implementation — 2026-10-22

**Firmware source committed, NOT yet built/flashed/verified on hardware.** The ESP32 `main/main.cpp` has been replaced with a circular-screen-aware black navigation-first home view: top phone connection status; large graphical left/right/straight maneuver indicator; next-turn distance and road; divider; permanent lower wideband AFR section showing `--.-` and `NO SENSOR` until real hardware exists. The previous four fake telemetry tiles are removed. BLE snapshot updates the navigation fields and phone connection indicator; no new BLE transport changes are needed for the existing test message `GC8|1|RIGHT|250 m|Main St`. The Android companion already contains a **Send test navigation to Waveshare** button, but **automatic live Google Maps-to-BLE streaming is NOT implemented**. No calibrated AFR colour thresholds, boost, swipe pages, phone call controls or moving map are implemented. The graphical arrow is a first-pass implementation and needs review on the real AMOLED before considering its styling final.

**Next:** In the ESP-IDF terminal run `git pull` and `build.bat`. If successful, flash only the development Waveshare, verify the navigation-first screen, then connect the Fold8 using the updated Android APK and press **Send test navigation to Waveshare**. Confirm the screen shows the arrow, `250 m`, `Main St` and `PHONE CONNECTED`. Stop and share build errors rather than flashing a failed build.

### Destination and notification shade direction — 2026-10-22

The owner wants a standard **top pull-down notification shade** eventually (Android-like interaction, incoming calls and notifications). **Design requirement recorded only; no gesture handling, shade, notifications or call controls implemented yet.** Preserve a top-edge gesture zone when implementing touch navigation and ensure critical AFR display remains visible/appropriately prioritized.

The navigation-first home screen now includes a small muted **`TO: <destination>`** line immediately below the next-turn street name and above the permanent AFR region. It shows `TO: --` without route data and `TO: Not provided` when a route message lacks destination. Firmware's existing `GC8|1|MANEUVER|DISTANCE|STREET` test format now accepts an **optional sixth pipe-separated printable ASCII destination field** (max 47 characters), preserving compatibility with older five-field messages. Android's **test button** now sends `GC8|1|RIGHT|250 m|Main St|Apex Motoring`. **This is illustrative test data, not an actual navigation destination from Google Maps.** Google Maps notification capture does not yet reliably provide the end destination; real integration must retain the destination entered in the Android companion and send it separately with navigation updates. Current BLE messages remain bounded at 100 bytes; long names require truncation/encoding strategy later. All changes committed; **neither new firmware nor new APK has been built, flashed or verified**. Review layout on physical round AMOLED after flashing, especially destination truncation and navigation/AFR spacing.

### 2026-10-22 — navigation HUD visual redesign (source committed; untested)
Owner approved the visual concept: **large smooth navigation arrows at left, distance/next street at right, separate two-line Destination label/value, subtle blue accent, always-visible bottom AFR**. Replaced the rectangular-block arrows in `main/main.cpp` with LVGL vector line paths for RIGHT, LEFT and STRAIGHT, rearranged the content into a side-by-side layout, kept destination separate, and reserved top space for the future notification shade. The note slot currently says NEXT TURN/NO ROUTE DATA; stop-sign and traffic-light data is **not yet available** from Android and must not be fabricated. Wideband stays `--.-` / NO SENSOR. The illustrative coloured AFR arc from the design reference is **not implemented** because no calibrated wideband input or warning thresholds exist. This is an LVGL vector implementation, not a PNG/SVG asset; it avoids reliance on unsupported image decoders. **No firmware build, flash, screenshot, or physical screen verification has occurred for this revision.** Next: `git pull`, `build.bat`, flash development board if build succeeds, then verify layout and existing BLE test payload visually. The current embedded fonts and 466px screen may require spacing/arrow refinements after actual hardware review.
