# GC8 Gauge Display

## 2026-10-24 — Consolidated pre-test firmware: diagnostics, power, and persisted preferences

This is the single firmware branch combining the previously queued Bluetooth/Gemini, parking-lot guidance, and orientation experiments with **on-device observability and safe power management**. It does **not** enable an AEM wideband, pretend measured boost/AFR data exists, or add deep sleep.

### On-device control (capacitive touchscreen)

| Where / gesture | Action |
| --- | --- |
| **Cockpit:** tap the Bluetooth status line near top centre | Open GC8 **System Status** diagnostics |
| **Cockpit:** tap elsewhere | Rotate clockwise 90° and lock; manual angle saved in NVS |
| **Cockpit:** hold touchscreen with device vertical, USB-down | Calibrate QMI8658 and enable saved 90°-step AUTO rotation |
| **System Status:** tap the **BRIGHTNESS** row | Cycle **40 / 60 / 75 / 90 / 100%**; persisted |
| **System Status:** tap the **IDLE DIM** row | Cycle **OFF / 1 / 5 / 15 min**; persisted |
| **System Status:** tap anywhere else | Return to cockpit |

A transparent top-level touch surface handles taps over the artwork/labels (background-only hit testing was unreliable). Coordinates are remapped for the currently selected hardware quarter-turn; this still needs real hardware testing because panel/touch transforms depend on Waveshare's implementation. BOOT and PWR button handlers are **unchanged** by this release.

### System Status diagnostics

The 466×466 on-device diagnostics show:
- **Companion GATT subscription** independently of **Voice Command HID** and **Media HID** subscriptions, including the number of HID reports submitted to NimBLE since startup. This does *not* prove Gemini actually launched.
- Active BLE link count, total connections, most recent NimBLE disconnection **reason code**, total disconnect events.
- **ESP32 uptime** (resets on every reboot), **ESP32 reset reason** (including brownout/power-glitch/watchdog/panic versus normal power-on), and a boot count stored once per startup under NVS namespace `gc8_boot`.
- AXP2101 **VBUS-good** from read-only PMIC status bit 5, **battery presence** from bit 3, genuine AXP2101 fuel gauge percent only when valid, and charging status only when a battery is present. USB present without a cell is **not** labelled "charging" or "0%".
- Screen rotation AUTO/LOCK angle and stored brightness/idle-dim controls.

### Safe display power management

Uses the Waveshare CO5300 brightness interface; no AXP2101 power-control registers are written. Default display brightness is **75%**. After **5 minutes** without touchscreen activity, with no active Maps route and diagnostic screen closed, the display dims to **25%**. Any touchscreen contact wakes full brightness. A Maps route or open System Status prevents dimming. Settings live in NVS namespace `gc8_display`.

**The processor, BLE/GATT, button polling, clock and notifications never sleep**: ESP32 deep sleep would break the garage and voice functions. This is a *panel power and AMOLED burn-in mitigation*, not a promise about battery runtime. No actual external-voltage/current/battery-capacity instrumentation is added.

### Reliability changes and deployment

- Adds a guard around NimBLE advertising restarts to avoid reconfiguring advertisements while already advertising; logs disconnect **reason** plus cumulative counts. The user should examine code 8/timeouts with the diagnostic screen if the phone still reconnects frequently.
- Restores the long-press gesture with the standalone transparent hit-target and reports meaningful feedback. Leaves previous voice HID report IDs, GATT custom service UUID, garage command, navigation packet formats, and Android app logic intact.
- Checks that `gc8_ble_start()` initializes NVS before preferences and boot counters are loaded. QMI8658 and AXP2101 use separate I2C addresses on the board's shared BSP-managed bus.
- **Automatic compile guard:** `.github/workflows/firmware-build.yml` builds firmware on GitHub Actions using Espressif's official ESP-IDF v6.1 container for `esp32s3`, on firmware pushes and pull requests. Check the [Actions tab](https://github.com/JohnathanKong/gc8-gauge-display/actions) for actual run results; no passing build is claimed by the assistant.
- **Build/flash once:** `git pull` and `.\\build.bat` in your ESP-IDF v6.1 terminal; flash using the existing process. If you haven't installed the earlier Companion BLE/navigation/voice diagnostic changes yet, update and install that Android APK too; this release doesn't modify the Android repository again.
- Suggested first stationary validation: cold boot and read **UPTIME/RESET/boot count**; toggle USB power; open diagnostics with a Bluetooth connection; disconnect/reconnect and watch **LAST DROP**; tap brightness/idle settings and reboot to verify NVS; test manual rotation and auto calibration. Only after that retest lower BOOT (short Voice Command and long media hold), upper PWR garage trigger, and Maps on a subsequent drive.
- **Verification boundary:** GitHub source review and static checks were performed. Firmware was **not compiled with ESP-IDF or flashed onto the 1.75C** here. These are code changes pending owner's build and hands-on validation; no claims of operational success are made.

## 2026-10-24 — Screen rotation for charging (1.75C, experimental)

The exact Waveshare **ESP32-S3-Touch-AMOLED-1.75C** has a QMI8658 IMU (I2C `0x6B`) and a CO5300 display with a BSP hardware rotation command. The charging connector can point sideways or up while the **entire** 466x466 dashboard (artwork, arrow, clock, status, battery, AFR) remains readable.

**How to use (no Android app control):**

- **Tap anywhere on the display:** Rotate one quarter turn (0° → 90° → 180° → 270° → 0°) and **LOCK** that orientation. A brief `ROTATION LOCK ...` banner appears. The manual setting survives power cycles. This works even if the IMU is missing.
- **For automatic orientation:** Hold the device **vertically, still, with USB pointing straight down**, then **press and hold the *touchscreen*** (not the BOOT/PWR button) for LVGL's long-press interval. After the `AUTO ROTATE ON` banner, the firmware records the current gravity sector as **physical upright (0°)** and stores it in NVS. From then on, it automatically counter-rotates the panel at the next steady 90° device orientation; the calibration persists across reboots.
- **Exit auto mode:** Tap the display once to rotate one step and lock. Long-touch while USB-down to recalibrate/re-enable auto. `HOLD UPRIGHT TO CALIBRATE` means the QMI8658 did not report a stable, sufficiently vertical orientation; move the unit upright, stop moving, and retry.
- Auto mode uses the IMU's **accelerometer** at 62.5 Hz (no gyroscope dead reckoning), sampling every 150 ms. It filters noise, requires about **1.5 seconds stable** before switching, ignores large acceleration and horizontal/face-up positions, and has a diagonal dead-zone to reduce chattering in a vehicle. A stationary face-up screen cannot infer compass/azimuth rotation from gravity alone.
- **Limit:** The full-frame panel rotates in 90° steps, **not arbitrary fractional angles** like 23° or 45°. Fully smooth arbitrary-angle text would require a separate software rendering approach and isn't implemented. This is deliberate for reliability/legibility.
- **Implementation:** `main/device_orientation.cpp` uses the BSP's *existing* I2C bus and its own QMI8658 device handle (the AXP2101 battery reading uses a distinct address). NVS namespace `gc8_orient` stores the manual angle and optional USB-down calibration. Missing/invalid IMU falls back to manual 0/90/180/270 without touching the dashboard or its BLE services. `main/main.cpp` invokes `bsp_display_rotation_set(...)` from the existing LVGL refresh task; no new Bluetooth characteristics, phone permissions, BOOT/PWR mappings or new touch buttons were added.
- **Build/test:** `git pull`, `.\\build.bat`, then flash with normal ESP-IDF workflow. No Android APK rebuild needed for this rotation feature. First test it **stationary**: tap through 4 orientations; then hold USB-down vertically for 2 seconds and long-touch the *screen* to calibrate; physically turn the device one quarter rotation and observe. If auto direction looks inverted, use manual taps until calibration behavior can be inspected. This source was not compiled/flashed on the hardware by the assistant.

## Garage button — current working configuration (2026-10-24)

**[Full end-to-end setup: Android Companion, Samsung Modes and Routines, SmartThings Garage Open/Close](https://github.com/JohnathanKong/gc8-gauge-display-android/blob/main/GARAGE_SETUP.md)**

Owner reports **working** after correcting an inaccurate SmartThings **Garage Door Opener** state. The upper PWR button on the exact round **1.75C** model is read on **GPIO3**, not the TCA9554 pin used in an obsolete development note below. The Samsung phone routine listens for a GC8 Companion notification containing **Garage Trigger** and turns the SmartThings **Garage Trigger** virtual switch ON. Two SmartThings routines each require **all** conditions: virtual switch ON and actual door respectively Closed (then Open door) or Open (then Close door); each resets virtual switch OFF after the door action. The Android notification phrase merely triggers the Samsung routine and **does not** identify a SmartThings device. An ESP32 toast reporting SIGNAL SENT **does not prove door motion**.

**Important:** Later hardware and routine fixes supersede the older dated 'inactive integration' passage at the end of this README. Use the linked guide, current source and the owner's tested configuration rather than following those historical notes.


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

### 2026-10-22 — actual navigation arrow assets (source committed, unbuilt)
Following physical screen feedback that LVGL vector line heads looked blunt, added `main/nav_arrow_assets.h`: **three generated, embedded 112×132 alpha-raster image assets** (LEFT, RIGHT, STRAIGHT) with solid shafts and pointed arrowheads. `main/main.cpp` now uses `lv_image_create` / `lv_image_set_src` to show these images instead of `lv_line` segments. These are actual embedded image assets, not the AI concept illustration and not runtime line drawing. The two-line destination and live BLE test payload remain intact. AFR still reads `--.-` / NO SENSOR; the colourful mockup arc is intentionally not represented as calibrated telemetry. Future notification shade and road-event icons remain planned, not implemented. **Build, flash, and physical appearance are not yet verified for this commit**; test on development Waveshare and inspect alpha image support, crispness and positioning before claiming a match to the reference.

### 2026-10-22 — combined visual polish pass (source committed; hardware test pending)
After owner review of the physical screen, batched multiple UI changes instead of individual asset-only iterations: regenerated RIGHT/LEFT/STRAIGHT embedded A8 alpha images with 4x supersampling for antialiased edges and a single pointed head; adjusted arrow position, next-turn text, two-line destination spacing, divider and AFR typography; added a **neutral grey decorative AFR framing arc**, explicitly not a calibrated coloured AFR scale. BLE test navigation and destination protocol unchanged. **Not compiled, flashed, or visually verified**. Next: `git pull`, `build.bat`, flash development Waveshare only after successful build; inspect sharpness, shape, text clipping, arc placement and BLE data. If errors occur, capture compiler output. The future notification shade, road-event notes and real wideband signal are still unimplemented.

### 2026-10-22 — approved arrow pass + AFR demo (committed, NOT built/tested)
- Regenerated embedded left/right/straight A8 navigation arrow assets with 4x supersampling, thinner shaft, shorter tail, curved bend and single pointed head. These are firmware-ready approximations of the approved visual reference; **verify appearance on physical screen**.
- Removed the defective LVGL AFR arc entirely (its knob appeared as a bright cyan dot and the curve overlapped text). Retained a clean divider and permanent AFR section. A future calibrated arc remains separate work.
- Extended the existing optional-destination navigation test protocol with an **optional seventh field**, e.g. `GC8|1|RIGHT|250 m|Main St|Apex Motoring|DEMO:14.7`. The parser accepts only a `DEMO:`-prefixed numeric test value. Five- and six-field navigation messages still work. The display renders `14.7` plus **DEMO ONLY**, never a misleading live sensor status. When demo data is absent or BLE disconnects, it displays `--.-` / **NO SENSOR**. No actual AEM sensor connection exists.
- The Android **Send test navigation to Waveshare** button sends the seven-field message in **one BLE write**, so no additional BLE characteristic or sequencing is required. This is a fixed test value, **not a live or calibrated AFR reading**.
- Firmware and Android changes are committed but **not independently compiled, flashed, or tested**. Rebuild BOTH repositories, flash development Waveshare and install updated APK; reconnect and press the test button. Check the arrow, destination, AFR value and DEMO ONLY status. Do not flash if build fails.


## Circular-safe cockpit redesign — 2026-10-23

The prior layout placed instrument text at y=445 and the battery marker near the top-right corner of a 466×466 circular screen. Both could be clipped despite being inside the square pixel grid. The current code replaces that draft with a safe-area layout:

- `main/cockpit_art.h` creates dedicated antialiased A8 graphic assets once at startup: shaded perspective roadway with lane markings, blue/red perimeter instrument arcs, a checkered destination flag, and a battery-outline symbol. These are composited as LVGL image sprites rather than approximated by a handful of line objects.
- `main/main.cpp` places all navigation text and both instrument labels inside the round viewport. Street and destination labels have fixed height and ellipsize overlong text, preventing overlap with the bottom gauges. Existing LEFT/RIGHT/STRAIGHT arrow image assets are preserved.
- The battery icon deliberately displays `--` until there is a **verified battery-voltage signal**. Boost likewise shows `--.-` until a sensor exists; demo AFR is labeled as such.
- Navigation BLE protocol, parsing, and Android companion have not changed.
- Visual design is based on a software circle-cropped preview; **firmware has not been compiled or verified on the physical device**. Build using the ESP-IDF terminal, flash only the development display, and inspect the rendered layout before further changes.

### 2026-10-23 — independent immediate destination rendering

ESP32 BLE parser now accepts strict, ASCII `GC8|1|DEST|<confirmed name>` (1–47 bytes of place name) as well as existing `GC8|1|CLEAR` and complete guidance packets. DEST stores the selected place name but deliberately clears prior arrows, distance, street, and any demo AFR values. On connection drop, stored place name is cleared. The LVGL dashboard renders a nonempty destination immediately, with `Getting directions...` in the maneuver area until Google Maps publishes an actual usable distance+turn. Complete guidance messages continue to replace the arrow, distance, street and destination exactly as before; CLEAR removes them all. No fabricated direction, distance or vehicle sensor data is shown. Update/flash both firmware and Android companion for this behavior; the previous ESP32 version cannot accept `DEST`. Source is committed but not compiled/flashed or hardware-verified.

### 2026-10-23 field-test redesign: legibility from the driver's seat

The owner tested the 466x466 round display at the actual intended dashboard distance. Most 16–26 px text was **too small to read while driving**, regardless of whether Google Maps destination parsing worked. The combined diagnostics file (17:09–17:54) also records 24 successful turn guidance BLE writes, 9 successful CLEAR writes, and 3 rejected DEST-only writes (GATT status 14), plus frequent Google Maps zero-distance notifications. Those failures do **not** establish a slow BLE transport. The display layout should not depend on a long, inconsistent destination string.

**Firmware UI:** `main/main.cpp` is now a high-priority, glanceable **navigation + AFR** driving view. There is **no BOOST readout**, **no visible destination/address row**, and no nonfunctional battery placeholder. The existing perspective road and decorative perimeter arcs remain. Turn icon is resampled from the approved A8 silhouette at boot from 112x132 to **140x165**; LVGL does not directly scale A8 images, so a bounded bilinear resampler generates three new A8 sprites at startup. Next-turn distance uses **Montserrat 40**, next street **Montserrat 32** across a broad two-line row (ellipsized if too long), BLE status **Montserrat 24** and persistent bottom AFR number **Montserrat 48** with label **Montserrat 20**. This deliberately prioritizes brief glance readability over showing a full address. None of these positions is designed to spill beyond the circular safe area. The BLE parser and Android companion continue to carry destinations, so a future detail page can restore them without new protocol work; no Android changes are required for this UI-only revision. AFR remains **--.-** until real wideband sensor data is integrated; demo AFR is visibly labelled `AFR DEMO`.

**Build note:** `sdkconfig.defaults` now enables LVGL fonts 32, 40 and 48. Existing local `sdkconfig` takes precedence and may still have them disabled. `build.bat` checks for these fonts and reports a clear error before compiling if missing. If it reports that error, use the ESP-IDF terminal and run `idf.py menuconfig`; use its search feature to find and enable `LV_FONT_MONTSERRAT_32`, `LV_FONT_MONTSERRAT_40` and `LV_FONT_MONTSERRAT_48`, save, and rerun `build.bat`. Pull firmware repository, build, and flash **only the development Waveshare** after a successful build. This revision is committed but **not ESP-IDF-built or physically tested**; check the real dashboard position and sunlight readability before treating the screen geometry as approved.

UI state note: BLE disconnected -> `Connect phone`, BLE connected but no active route -> `Start navigation`, destination-only packet after route start -> `Getting directions...` until a real nonzero-distance instruction arrives. The destination payload is used as a route-active marker but is not drawn on the driving screen. No actual hardware render has been validated for this update.

### 2026-10-23 follow-up — original arrow, fewer Android permissions

Owner confirmed the original **112x132** navigation arrows were already readable; reverted the experimental 140x165 alpha-image resampler and restored the original 112x132 silhouettes and position. Retained large 40px next-turn distance, 32px street, 48px AFR, 24px connection status, no boost, no destination text. The firmware now accepts strict `GC8|1|START` as an additional BLE packet, sets `route_active=true` but clears stale arrow/road/distance. UI displays `Getting directions...` while active with no valid turn. `CLEAR` and BLE disconnect end the route; a real turn packet marks it active and displays its valid guidance. The former DEST packet remains understood for backwards compatibility, but the new Android companion does not send one. The phone may send an empty destination field in each turn packet for backwards protocol compatibility. No navigation URL or destination address is needed, so Android Accessibility service was removed. **Flash matching firmware and install matching Android APK** to use START; the previous firmware rejects this new packet but still supports ordinary guidance.

Commits were pushed; no ESP-IDF build, APK build or actual display field test has been performed for this revision. Existing ESP-IDF local sdkconfig may require LVGL fonts Montserrat 32, 40 and 48 enabled in menuconfig as documented above.

### 2026-10-23 — owner-approved direction for readability and automatic font build

The owner rejected the first large-font stacked screen after seeing a code-based screenshot preview, because it looked cramped, navigation alignment was wrong, and the former AFR/boost divider made no sense with boost removed. **Current revision supersedes the earlier stacked layout:**

- Split two-column navigation: original field-tested 112×132 arrow at left, prominent Montserrat 40 distance **below the arrow**, and a large Montserrat 32 multline street on the **right**; long street names ellipsize instead of shrinking.
- Top branding and Bluetooth state remain restrained. Removed the decorative perspective roadway, boost, destination, old blue/red perimeter arcs, and all horizontal/divided gauge ornaments from this driving screen.
- AFR is centered below the navigation, Montserrat 48 value with Montserrat 20 label. A single **U-shaped, semicircular blue cradle** wraps beneath the AFR value, implemented as a static 280×126 A8 alpha image rendered by `paint_afr_cradle()`. It has **no arc knob**, no calibration scale, and does not imply an actual measured AFR. Display remains `--.-` until a wideband signal is connected.
- Layout coordinates are in `main/main.cpp`. A code-based rasterized screenshot preview (not actual hardware rendering) was produced for review before flashing. The design is not physically verified yet.

**Build failure fixed:** The owner's Windows terminal reported `ERROR: Montserrat 32 font is disabled in your local sdkconfig` after pulling these UI changes. That was due to a stale, locally generated `sdkconfig`: `sdkconfig.defaults` alone never overwrites an existing `sdkconfig`. Instead of requiring interactive menuconfig, `build.bat` now invokes `python tools\\ensure_lvgl_fonts.py` to enable only `CONFIG_LV_FONT_MONTSERRAT_32=y`, `40=y` and `48=y` in the local config, then executes `idf.py reconfigure` and `idf.py build`. The helper was tested twice against a copy of the previously archived project `sdkconfig`: the first pass enabled each flag once; the second pass was idempotent. **The actual Windows build and Waveshare flash remain unverified.**

One-path next step in VS Code's **ESP-IDF terminal** on Windows:

```powershell
git pull
.\\build.bat
```

Wait for a successful compile before using ESP-IDF Flash Your Project on the **development board**, not the factory-reference unit. If compilation fails, capture the first actual compiler/configuration error; do not assume the new UI or local SDK automatically works.


### 2026-10-23 — real clock header, subtle road, divider, future AFR colour

Owner's latest feedback on the two-column preview: keep the left arrow + large distance / right street layout; replace the GC8 logo with **current phone-local time**; restore the divider and subdued perspective road; retain the clean U-shaped AFR cradle but eventually recolour it based on actual wideband AFR data.

The new BLE parser accepts exactly `GC8|1|TIME|HH:MM:SS|12` or `|24`, validating digits, positions, and 24-hour/60-minute/60-second ranges. The `Gc8BleSnapshot` stores synchronized seconds since local midnight, 12/24-hour preference, and ESP32 monotonic timestamp from `esp_timer_get_time()`. The LVGL timer derives the updated time on each refresh independently of the navigation snapshot so **minute changes appear even if Maps produces no notification**. It displays `7:45 PM` in 12-hour mode or `19:45` in 24-hour mode, based on the connected phone. Before the first Bluetooth clock sync after a power cycle it honestly shows `--:--`; while disconnected after syncing it continues on the monotonic clock until the next sync. Android refreshes this every 60 seconds, covering drift and timezone/DST changes when connected.

The perspective road is now returned as a **faint 40%-opacity, behind-guidance** A8 blue sprite at (73,186), using LVGL's image_opa property. A narrow horizontal blue divider at y=307 separates navigation from the AFR. The established 112×132 arrow and spacious split stay untouched; no destination or boost field is restored. The AFR cradle remains blue and static because **no live calibrated wideband reading exists yet**. A pointer to the LVGL cradle sprite is retained so it can respond to real AFR values later once the ECU/wideband signal and meaningful context-dependent colour mapping are defined. A single raw AFR threshold cannot safely distinguish cruising from on-boost mixtures, so we do not imply green = safe or red = dangerous without real data.

Both new firmware and Android Companion must be built and deployed for the clock feature. No hardware build/flash or road test has been performed for this revision. An updated code-based 466×466 preview is available in the active conversation, but is **not an LVGL framebuffer capture**. Existing auto-migration of LVGL Montserrat 32/40/48 fonts in build.bat remains in place.

### 2026-10-23 — blue phone clock and AEM-style segmented AFR sweep (supersedes smooth cradle)

The owner requested that the **phone-synced time text be blue**, matching the existing turn arrow, and disliked the smooth glowing U-shaped arc beneath AFR. The new firmware sets the LVGL `clock_label` colour to `kBlue` while preserving its existing phone-clock synchronization and font size.

The formerly continuous `afr_cradle` alpha sprite has been replaced by an **AEM-wideband-inspired 24-segment lower AFR sweep** (`cockpit_art::afr_sweep`, 280×126 A8 pixels at x=93, y=321). The procedural rasterizer `paint_afr_sweep()` uses 24 discrete, gently illuminated blue blocks around the lower half of an ellipse, with stronger major graduations every six blocks. Segments are static/unselected; the large AFR number remains `--.-` until real wideband input is wired in. No current reading, needle position, colour-coded safety threshold, or live AFR response is simulated. Once real measurements and a defensible colour mapping are available, the stored `afr_sweep_sprite` handle can support a future active overlay or recolour. This gauge styling is purely visual.

The **original 112×132 left turn arrow**, 40px distance below it, 32px street on the right, faded blue road (40% opacity), horizontal divider, and 48px AFR value remain unchanged. **No Android repository changes** are required for this cosmetic update. The two new firmware source files are `main/main.cpp` and `main/cockpit_art.h`. This revision has a code-based 466×466 preview in the conversation, but has **not been compiled with ESP-IDF or photographed/tested on Waveshare hardware**. The previous automatic LVGL 32/40/48 font migration remains in `build.bat`. Build from VS Code's ESP-IDF terminal using `git pull` then `.\\build.bat`; flash the development board only after a successful build.

### 2026-10-24 — field-layout refinement: wider AFR arc and brighter road

Owner feedback on the first blue clock/segmented AFR preview: the arc was too narrow for the intended wideband readout and the road was far too dim. This revision changes only the ESP32 display styling:

- Recentered a wider 30-segment AFR sweep around the existing central AFR label/value. Sprite dimensions are now **376×136** at **(45,321)**, with elliptical center x=187.5, radius x=166 and radius y=116. The illuminated sweep occupies roughly x≈51–415, compared with previous x≈93–373, taking advantage of more of the circular 466px screen. The larger sprite includes transparent margins on all four sides so no segment is cut off. Geometry was inspected programmatically: its alpha mask has **zero nontransparent pixels beyond the circle**, and no illuminated sweep pixels reach any sprite border.
- Boosted perspective road sprite opacity from **40% to 80%**; increased outer lane markings (50+63*t → 105+125*t), inner lines (23+47*t → 48+90*t) and center dashes (55+50*t → 110+100*t). This makes the roadway unmistakable while full-opacity direction text is drawn above it.
- Kept the 112×132 arrow, left distance, right street, blue clock, subtle divider and central unread-live-AFR value unchanged. The 30 blue sweep segments are still purely decorative; no AEM measurement or calibrated danger threshold is connected.

Updated `main/main.cpp` and `main/cockpit_art.h`; there are no Android changes. New 466×466 visual simulation and before/after comparison were generated in the conversation using the same raster formulas and coordinates. **These are not actual ESP-IDF/LVGL framebuffer screenshots and were not verified on physical Waveshare hardware.** Pull current firmware, build with `.\\build.bat` in the ESP-IDF terminal and flash the development device only after successful build.

### 2026-10-23 — full-circle visual implementation from approved concept

The owner strongly approved the generated cinematic circular cockpit concept and explicitly requested a **real firmware implementation**, including ALL separators and use of the full circle, with navigation shifted left to give the street more space. This revision implements a maintainable LVGL approximation rather than embedding fake numeric values or a static screenshot.

**Source implementation:** `main/main.cpp` now draws two concentric rim borders; faint top-left/top-right rules; a vertical separator at x=194 between nav arrow and street; and a full-width horizontal separator across x=42..424 at y=310. The original approved 112x132 arrow moves from x=83 to x=44, with a 40px distance below it; street text moves from x=233 to x=211 and widens from 172 to 224px, with 32px type for shorter names and 26px for long compound names. The blue clock remains at top center using 40px Montserrat; BLE state remains immediately below. The upper-right battery now displays the **real Android phone battery**, never an invented percent; hides when unavailable.

`main/cockpit_art.h` replaces narrow rectangular decorative assets with two procedurally rasterized **full-width A8 masks** allocated persistently in ESP32-S3 PSRAM (via `heap_caps_malloc`). The perspective roadway covers x=0..466, with strong blue converging outer/inner lane markings and dashed center line behind navigation. The 30-segment LED-inspired AFR sweep follows an actual circular radius (~205px from screen center 233,233) across approximately x=36..430 and y=305..448, rather than a narrow U. It is purely decorative and blue; until a physical calibrated wideband feed exists, AFR remains `--.-`, or is explicitly marked DEMO if a test value is used. `main/CMakeLists.txt` declares heap/esp_timer dependencies. All styling is static and drawn once at startup; navigation/clock/battery/AFR numeric text remains dynamic LVGL labels.

**Protocol:** TIME packet is backward-compatible with an *optional* validated percent suffix: `GC8|1|TIME|09:42:05|12|87`. It retains the old version without suffix and refuses invalid percentages or malformed times. Phone percentage comes from Android's sticky `ACTION_BATTERY_CHANGED` and needs no extra permission. Phone battery is not a vehicle voltage sensor.

**Validation/limitations:** The reference picture is an aspirational generated illustration with glossy bloom and sample 14.7/87% values, **not** a software screenshot. A code-based 466px preview using the implemented coordinates/arc/road math was exported into the conversation. The revised AFR mask was checked geometrically and has **zero pixels outside the circular display bounds**. The actual ESP-IDF firmware has not been compiled, flashed, or verified on physical hardware by the assistant. The initial on-device display may differ from the concept in text rendering, gradients, glow and antialiasing. PSRAM allocation errors are logged and omit only the corresponding decorative image, preserving basic navigation UI.

**Next validation:** Pull both repos, build firmware in ESP-IDF terminal via `git pull`, `.\\build.bat` and flash the **development Waveshare** only after a successful build. Rebuild/install Android Companion (as its TIME packet now carries phone battery); old app remains usable for time/nav without battery percent. Verify real BLE time, phone battery, long-street wrapping, no clipped circle elements, and navigation START/CLEAR without accessibility.

### 2026-10-23 final layout pass — reference-matched halo and boot-time work

After checking a **466x466 code-based pixel preview**, added four very-low-opacity (20%) offset silhouettes behind each approved 112x132 navigation arrow. The full-intensity foreground arrow stays its original size. These halos update/hide alongside LEFT/RIGHT/STRAIGHT and are deliberately restrained on the real OLED. Confirmed the top rules, vertical separator, horizontal AFR divider, double concentric bezel, full-width perspective road, 30-segment near-rim AFR and live phone battery display are all present in current source. Moved the PSRAM artwork generation to `app_main()` *before* `bsp_display_start()` / acquiring the LVGL lock; the heavy one-time rasterizer never runs during the 250ms refresh or inside an active LVGL render lock.

Final preview files in the conversation: `gc8_reference_implementation_466.png` and `gc8_reference_implementation_comparison.png`. The preview is an approximation made from the firmware's layout and procedural artwork mathematics, not a physical framebuffer capture. A mask check found no AFR pixels outside the circular display. The code is pushed but **not ESP-IDF-compiled, flashed or visually verified on Waveshare hardware**. There is no fabricated 87% or 14.7 reading: phone battery is populated only from BatteryManager and AFR defaults to `--.-` until the calibrated wideband sensor is integrated.

Build/install both repositories: firmware `git pull`, `.\\build.bat`, then flash the development Waveshare after a successful build. For Android, `git pull` and `docker compose run --rm android gradle --no-daemon assembleDebug`, then install APK. The earlier build.bat font migration remains in place; it enables Montserrat 32/40/48 and reconfigures ESP-IDF automatically. If compilation fails, capture the first error line; do not flash a failed build.

### 2026-10-24 — genuine Waveshare battery + polished idle states (supersedes phone battery)

The owner confirmed that the upper-right battery icon in the approved UI means the **Waveshare DEVICE's own battery**, NOT the Android phone or the car, and that the no-navigation view looked unfinished. Both issues are addressed without changing the existing active-navigation screen.

**Device battery:** `main/device_power.{h,cpp}` is a read-only task for the board's AXP2101 PMIC on the existing BSP-owned shared I²C master bus (7-bit address 0x34), using `bsp_i2c_get_handle()`. It validates chip ID 0x4A from 0x03, battery-present bit 3 in PMU_STATUS1 (0x00), and genuine 0–100% fuel-gauge state from 0xA4; charge direction is read from status register 0x01. Readings are cached thread-safely using `std::atomic` and polled every 15 seconds in a small FreeRTOS task (no I²C in LVGL timer). A batteryless unit, unplugged cell, communication error or invalid gauge hides the icon and percent completely; no fabricated percent or phone substitution. Valid readings display even when Bluetooth is disconnected. `refresh_navigation()` compares battery state BEFORE the previous BLE-snapshot early return, so battery updates do not depend on Maps messages. Added `device_power.cpp` and `esp_driver_i2c` to the CMake main component. Firmware's legacy TIME parser accepts/validates an optional earlier Android phone-battery suffix purely for backward compatibility but **ignores it**; it does not store/display phone battery.

**No navigation UI:** The bold arrow/distance/street/vertical divider now hide together when no real maneuver exists; a rounded dark-blue translucent status panel fills the upper navigational half instead, preserving clock, genuine battery, surrounding circular rim, blue horizon divider, road and the permanent AFR gauge. The panel presents distinct readable states:
- BLE disconnected: `GC8 COMPANION` / `CONNECT PHONE` / `OPEN COMPANION TO PAIR`;
- BLE connected but no Maps route: `NAVIGATION` / `READY` / `START GOOGLE MAPS`;
- Maps sent START but no positive turn distance: `GOOGLE MAPS` / `ROUTE STARTED` / `WAITING FOR FIRST TURN`.
When a real valid turn packet arrives, the entire overlay hides and the approved split arrow/distance/street arrangement returns automatically. On CLEAR/end or BLE disconnect it reappears. Changes are purely UI/firmware and do not request more Android permissions.

**Preview and limits:** Three code-based 466×466 layout renders are available in this conversation (`gc8_new_idle_ready.png`, `gc8_new_idle_starting.png`, `gc8_new_idle_offline.png`, plus `gc8_new_idle_states.png`). Preview time and battery values are illustrative, not sensor test results. The AXP2101 firmware integration is grounded in Waveshare's 1.75C BSP, the PMIC register map and official Waveshare hardware documentation; **not yet ESP-IDF compiled or physically tested** by this assistant. The user's current older Android app will keep navigation and clock functional; firmware deliberately ignores any old phone percentage. Firmware-only build `git pull` / `.\\build.bat` from ESP-IDF terminal then flash development board after success. If board battery isn't connected, expect icon hidden. The sensor check does not read or alter charging current, power rails or PMIC configuration.

### 2026-10-23 — protect dashboard from stale final turn after arrival

The user reported the last navigation turn persisted on the round screen after reaching a destination. Their Android log showed the first route correctly sent CLEAR at 21:09:10, while the second did not transmit CLEAR because it was queued behind an incomplete TIME write after Maps stopped publishing valid guidance. Google Maps reached 0m at 21:25:59 and did not remove the navigation notification until 21:28:55. **No reliable explicit arrival event was provided in that log.**

Firmware protection: an incoming `GC8|1|HOLD` control packet keeps `route_active=true` while clearing the obsolete maneuver and showing `GUIDANCE PAUSED / WAITING FOR MAPS` in the same attractive idle panel. This makes no arrival assertion and is safe at intermediate turns; new valid guidance immediately restores the normal left-arrow/right-street UI. Independent firmware fallback checks the age of the last accepted full maneuver using ESP32's monotonic clock. When a turn distance is **at most 50 metres** and NO fresh valid instruction arrives for **20 seconds**, the returned LVGL snapshot suppresses its old arrow and instead displays GUIDANCE PAUSED; a later turn supersedes it automatically. Distant turns remain displayed if Maps briefly stops updating. An actual Maps notification-removal/explicit-arrival CLEAR packet displays the normal READY state. BLE connection loss already clears current guidance on the device.

Requires flashing updated Waveshare firmware and installing matching Android Companion for HOLD. Source changes are committed but NOT ESP-IDF compiled, flashed or field-tested by the assistant. Clock, device battery, genuine AFR placeholder, road/halo/dividers and approved full-circle aesthetic remain unchanged. Test only while parked or review logs later: near destination look for `NEAR_ZERO`, `GUIDANCE_HOLD`, `BLE_SEND GC8|1|HOLD`, and finally `NAV_ENDED` and successful `GC8|1|CLEAR`. Missing CLEAR still clears on disconnect; if no clock and no guidance arrive for a near turn, local 20-second fallback does not depend on Android sending any packet.

### 2026-10-23 — Meross garage door upper PWR button: safe inactive integration scaffold

Owner wants a single tap of the **upper physical side button** to toggle a Meross garage door and a temporary downward-sliding banner on the round display. No permanent open/closed status indicator. Official Waveshare ESP32-S3-Touch-AMOLED-1.75C physical schematic, page 3, places **PWR above BOOT**. PWR is the AXP2101-controlled upper key, indirectly visible on TCA9554 I/O expander **P4 / SYS_OUT**; lower BOOT remains GPIO0, completely untouched. We use the BSP's **existing TCA9554 handle** via `bsp_io_expander_init()` and read only `IO_EXPANDER_PIN_NUM_4`, so no conflicting second I2C device registration, no writes to PMIC power configuration and no changes to BOOT/flashing. `main/garage_button.cpp` polls at 35ms, debounces for three samples, acts only on a released short press (75ms–1200ms), and enforces a 6-second local cooldown; a long PWR press still belongs to native power management.

Bluetooth transport: existing service and write characteristic remain unchanged. A second notify-only GATT characteristic uses `a8e31c40-6b8f-4b67-a4f0-4a0b75000003`. When subscribed, a button press emits exactly `GC8|1|GARAGE|PRESS` (18 bytes, fitting minimum ATT MTU); if disconnected/unsubscribed the UI reports `GARAGE: PHONE OFFLINE` and no request is emitted. The existing inbound write characteristic accepts the strictly enumerated `GC8|1|GARAGE|RESULT|NOT_READY`, `|SENT`, or `|FAILED`. These correspond to `GARAGE NOT SET UP`, `GARAGE CMD SENT`, or `GARAGE CMD FAILED` on screen. `SENT` is explicitly *only submission acknowledgment, not verified door motion*. The event is generated ONLY in firmware in response to the actual PWR tap; firmware stores no garage credentials and makes no Wi-Fi or Meross request.

UI: an unobtrusive 354x62 px blue-outline toast animates down from y=-64 to y=113 over 230ms and hides after 3.5 seconds. It overlays the navigation briefly without moving the approved arrow/street, blue clock, road, AFR or rim, and works equally while idle. The toast evaluates its expiry before the normal unchanged-BLE-snapshot early return, so it disappears even when navigation is idle. Top and lower hardware buttons are not remapped except the new short upper tap.

**Crucial deployment status:** Real Meross triggering is NOT connected yet because the owner has not identified their controller model, access path (Meross app alone versus Home Assistant/local gateway) or authorization. The Android app explicitly sends `NOT_READY` in response to each press. This is intentional: never actuate a physical garage door or claim toggle success during an unconfigured test. Next step is determine Meross integration path, implement authenticated command with correct open/close logic and no exposed credentials, then change the response to SENT/FAILED. For a Home Assistant cover entity, verified `cover.toggle` is a potential backend action, but DO NOT assume HA is present. Security review for BLE trust is required before enabling live door control.

Firmware source touched: `main/garage_button.h`, `main/garage_button.cpp`, `main/gc8_ble.h`, `main/gc8_ble.cpp`, `main/main.cpp`, `main/CMakeLists.txt`. These changes are COMMITTED but **not compiled with ESP-IDF or bench-tested on actual buttons/garage**, since the assistant runtime has no ESP-IDF SDK. First test must be stationary and with the garage door safely isolated/unconfigured. A real long press may still be handled by AXP2101 power control, so do not change PMIC hold behavior.


### 2026-10-24 — physical buttons: upper garage, lower native Gemini HID voice
**Current implementation** replaces the earlier lower-BOOT Companion app-launch event with standard Bluetooth HID-over-GATT.

- Upper **PWR** (GPIO3, active HIGH) retains its opt-in existing BLE garage routine event, `GC8|1|GARAGE|PRESS`. No SmartThings, notification, or garage control behaviour was changed.
- Lower **BOOT** (GPIO0, active LOW) samples a short press and release (75–1200ms, with debounce/cooldown), and emits a standard HID Consumer Control report containing usage `0x0CF` **Voice Command** (report 1, one-bit input, pressed then released). Android maps it to `KEYCODE_VOICE_ASSIST`. Android chooses the current default digital assistant; the intended selection on Fold8 is **Gemini**, not the retired Google Assistant.
- The ESP32's existing NimBLE host now exposes its previous GC8 custom service **and** HID service `0x1812` on the same server. HID Information `0x2A4A`, Report Map `0x2A4B`, HID Input Report `0x2A4D`, Report Reference `0x2908`, HID Control Point `0x2A4C`, Protocol Mode `0x2A4E`, and Device Information PnP ID `0x2A50` are included. GAP Appearance advertises generic HID (`0x03C0`). HID uses its own notify/connection handle and requires an encrypted bonded BLE host. The existing navigation/clock and garage characteristic UUIDs are unchanged. No Android app-launch event is emitted by BOOT.
- Firmware records BLE bonding keys in NVS. `sdkconfig.defaults` enables NimBLE NVS persistence and multi-connection support; `build.bat` runs `tools/ensure_nimble_hid.py` to update a pre-existing local `sdkconfig`, since defaults do not overwrite saved config.
- **Setup:** Build with `git pull` / `.\\build.bat` from the active ESP-IDF terminal, then flash. Stop GC8 Companion's background service briefly; in Fold8 Android **Settings → Connections → Bluetooth**, pair **GC8 Display** as an input device (remove an older saved pairing and re-pair if Android has cached the old non-HID GATT database). Confirm Gemini is your preferred digital assistant in Android settings. Start Companion service again for navigation, garage and clock.
- **Physical verification:** With the Waveshare fully booted, short-press lower BOOT. **PAIR VOICE REMOTE** means the HID host has not subscribed or encrypted/bonded. **VOICE KEY SENT** means the press/release report was delivered to NimBLE, not that Gemini started listening. The hardware can still enter ROM download mode if BOOT is held while resetting. Pressing upper PWR must continue to affect **only** the opt-in garage event.
- This branch has been source-reviewed but **not ESP-IDF compiled, flashed, paired or tested on the Fold8** in the assistant environment. Android may treat external voice key launches differently with the screen locked. No Android companion background service or third-party permission bypass is required for the HID input itself.

### 2026-10-24 — parking-lot and destination-side text guidance (requires Companion update)

The Android Companion now recovers from BLE connection changes when Fold8's bonded HID host and Companion's custom GATT client coexist, by alternating a known bonded identity connection attempt with UUID/verified-address scanning. The ESP32 keeps the **existing dual GATT services** (custom navigation/garage plus encrypted bonded HID Voice Command), service identifiers, legacy turn commands and upper PWR garage behavior. No voice mapping, NVS or button timing changes were made.

Firmware accepts a new strictly validated `GC8|1|INFO|<TITLE>|<HINT>` inbound write packet (printable ASCII, title 1–31 bytes, hint at most 39 bytes) as **text-only active-route guidance**, without manufacturing a bearing, distance, destination arrival or turn arrow. Two representative packets are `GC8|1|INFO|EXIT PARKING LOT|TOWARD COURTYARD LN` and `GC8|1|INFO|DESTINATION RIGHT|FOLLOW GOOGLE MAPS`. The circular cockpit uses its existing centered guidance/status panel to render these messages; its arrow/distance region stays hidden. Fresh LEFT/RIGHT/STRAIGHT messages clear text-only guidance automatically, and START/HOLD/CLEAR, BLE disconnect and new destination control events reset the text state. Remaining clock, battery, AFR and garage overlays are untouched.

Compile in the ESP-IDF terminal using `git pull`, `.\\build.bat`, flash only after successful build, then update the Android APK in the companion repo. The previous firmware would reject new INFO messages, so both updates are required to see the new parking/side screens. No ESP-IDF build or physical display validation was available to the assistant; actual Android/Fold8 connection durability remains to be established on the car.

### 2026-10-24 — independent lower BOOT voice/headset gestures (experimental)

The **upper PWR** button and GC8 garage/navigation/INFO GATT protocol are unchanged. Lower **BOOT (GPIO0)** now supports two deliberately distinct actions while firmware is running:

- **Short tap** (75–849ms): the previous bonded, encrypted BLE HID Consumer Voice Command `0x0C:0x0CF`, report ID **1**, 90ms down/up. Still controlled natively by Android; does not require Companion and makes no promise of lock-screen activation.
- **Long physical hold** (850ms–4.5s, release before starting another press): an **experimental headset-style** BLE HID Consumer Play/Pause `0x0C:0x0CD`, report ID **2**. After button release, a worker sends a 950ms held media press and release, independently of garage polling. Android's *USB headset* specification documents long media button presses triggering hands-free voice search when locked; Samsung's behavior for **this custom BLE HID device is not guaranteed**. The gesture might instead play/pause music, do nothing, or launch an unrelated search UI. It does not emulate the system Power/Side key or bypass keyguard.
- Two separate 0x2A4D Input Report characteristics have matching 0x2908 Report Reference descriptors (ID 1 Voice, ID 2 Media) in one 0x1812 HID service. The original voice key and original custom GATT service are preserved.
- **Diagnostic without ADB:** when Companion is connected/subscribed on its separate GATT profile, the Waveshare emits short `GC8|1|VOICE|TAP`/`HOLD` events and results `SENT`/`MEDIA`/`FAIL`/`UNSUB`. Android DriveLog records these as `VOICE_BUTTON`. These only establish detection and BLE submission; they **cannot confirm Android received/acted on the HID report or that Gemini opened**. If only HID is paired but Companion is disconnected, the display still provides short messages `VOICE KEY SENT`, `HEADSET KEY SENT`, `PAIR VOICE REMOTE`, `MEDIA KEY NOT PAIRED`, etc.
- **Bluetooth cache warning:** The HID report map and GATT table have changed. After flashing, **forget/unpair GC8 Display** in Fold8 Bluetooth settings, then pair again. This can also change the bonded BLE address seen by Companion; its previous verified-address reconnect attempt falls back to service scanning once the newly paired custom GATT service becomes available.
- Test while **stationary**, phone unlocked first, then on lock screen. Compare short tap vs long hold only with music stopped, because media Play/Pause may toggle playback. Neither success nor failure of a single key event proves any security bypass. Avoid holding BOOT across device reset because GPIO0 is an ESP32 flash-boot strap. No Android APK-side voice assistant launcher or additional buttons are introduced.

Build/flash firmware: `git pull` and `.\\build.bat` in ESP-IDF, then flash normally after successful build. Build/install updated Companion to capture `VOICE_BUTTON` entries. Source changes have **not** been validated by compiling ESP-IDF or exercising the Fold8. Android compatibility of headset-style activation must be verified on the physical handset.
