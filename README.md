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
