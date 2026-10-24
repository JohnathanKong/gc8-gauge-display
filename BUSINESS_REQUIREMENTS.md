# GC8 Digital Companion — Business Requirements & Continuation Guide

**Document ID:** GC8-BRD-001  
**Version:** 1.2  
**Last updated:** 2026-10-21  
**Status:** Working baseline; individual integration capabilities require validation  

> **Garage integration update — 2026-10-24:** Owner reports the upper Waveshare PWR → BLE → Android GC8 Companion → Samsung Modes and Routines → SmartThings **Garage Trigger** virtual switch → **Garage Open / Garage Close** routines works after correcting an incorrect SmartThings garage-door state. SmartThings routines require **ALL conditions** (virtual switch ON plus actual door Closed/Open) and reset the virtual switch to OFF after the door command. The exact Garage Open configuration was documented by screenshot; Garage Close was reported working but not separately pictured. This is a later convenience feature, separate from the original phone/navigation milestone. **[Current installation, exact routines, test steps, failures and security limitations](https://github.com/JohnathanKong/gc8-gauge-display-android/blob/main/GARAGE_SETUP.md).** Dated milestone checkboxes and older garage notes below describe their original historical state and should not be interpreted as overriding this update.

**Source of truth:** This file for product scope and decisions; `README.md` for build/run instructions; Git history for actual implementation.  
**Project:** GC8 Gauge Display  
**Repository:** https://github.com/JohnathanKong/gc8-gauge-display  
**Target vehicle:** 1996 JDM Subaru Impreza WRX STI Version 3 (GC8, EJ20K)  
**Display:** Waveshare ESP32-S3-Touch-AMOLED-1.75C, PCB marking 1.75C Rev 1.1  
**Development:** Windows 11, VS Code, ESP-IDF v6.1, C++, LVGL, Waveshare board-support package

> **For a new chat / coding agent:** Read this entire file, `README.md`, `main/main.cpp`, `main/idf_component.yml`, `sdkconfig.defaults`, `build.bat`, and recent Git commits **before** proposing changes. Respect the verified-vs-planned distinctions and checklist below. Continue from the first unchecked task in the active milestone, not from a new architecture proposal. Ask only for genuinely missing details; do not make assumptions about phone OS or unsupported APIs. Do not claim code has been tested on hardware without user confirmation. Keep README and this document updated with every meaningful milestone.

---

## 1. Executive summary

Build a discreet, purpose-built **digital companion**, not a replacement ECU, stereo, factory cluster, or smartphone. The circular AMOLED display should look integrated with a 1990s motorsport-era GC8 while providing glanceable modern information: phone-sourced clock, navigation, now-playing metadata, incoming call/status information where supported, and eventually independently acquired engine telemetry. The system must boot directly into the GC8 interface without an app launcher.

The owner has **already successfully built, flashed and seen the custom GC8 LVGL dashboard on the physical Waveshare board** (confirmed in conversation on 2026-10-21). The current firmware displays placeholder engine values because no vehicle sensor/controller hardware is yet connected. **The immediate priority is **Android navigation integration**, not gauges. The owner prefers a full moving map but accepts arrow + distance + street name as the first deliverable.**

The car has an existing **Bluetooth audio head unit**. All music playback, track control, volume, call audio, speakers, and microphone are handled by that head unit/phone. **The Waveshare is display-only for media and calls: no playback buttons, no volume controls, no answer/decline controls, no audio streaming, no microphone or speaker requirement.** This is an explicit scope decision.

## 2. Product principles (non-negotiable)

1. **Preserve the car:** keep the GC8's analog, mechanical character; do not splice original ECU wiring without a separately reviewed, non-destructive plan.
2. **One-purpose firmware:** power on -> embedded firmware -> GC8 UI. No Android, Linux, generic launcher, or bundled apps on the display.
3. **Display-centric, not display-dependent:** the Waveshare presents data and simple view navigation; a future hidden controller performs ECU/sensor acquisition and logging.
4. **Phone is the connected-services source:** phone handles navigation, media, call status and internet-dependent features.
5. **Head unit owns audio and playback control:** the gauge must never compete with existing Bluetooth head-unit functionality.
6. **Never fake telemetry:** unknown/unavailable/stale readings must show clear placeholders/status, not plausible-looking fabricated values.
7. **Fail safely:** loss of BLE/phone/controller must not freeze the display or affect vehicle operation.
8. **No cloud dependency for core UI:** prefer local communications; cloud features are optional and must be explicit.
9. **Small, glanceable UI:** designed for a 466×466 round AMOLED, not a shrunken phone app. Avoid distracting interactions while driving.
10. **Source-controlled, reproducible:** keep code, requirements, decisions, and exact build steps in the repo; no secrets in Git.

## 3. Architecture and ownership of responsibilities

### 3.1 Waveshare display — currently available and flashed

- ESP32-S3 with circular AMOLED and touch.
- Runs custom C++ ESP-IDF firmware using LVGL and Waveshare BSP.
- Renders screens, status, basic touch navigation, and incoming normalized messages.
- May maintain last known clock and connection state; must mark outdated information.
- Should not perform SSM1 ECU protocol acquisition, wideband signal processing, or authoritative telemetry logging.
- Physical installation should use a discreet power cable; BLE preferred for data.

### 3.2 Smartphone — next integration target

- Supplies clock/timezone, navigation guidance, media metadata, and optional call/notification metadata.
- A dedicated companion application is **permitted if required**, but exact phone OS is **not yet confirmed**. Platform choice must be obtained before implementation.
- Existing head unit remains the Bluetooth audio endpoint.
- Phone-to-display transport preference is BLE with explicit pairing/reconnect semantics; feasibility must be checked on the selected platform.
- **Do not assume Google Maps provides an unrestricted live turn-by-turn API** or that iOS/Android expose caller IDs/media metadata to arbitrary BLE peripherals. Determine legal, supported platform integration first.

### 3.3 Hidden vehicle controller — future phase, hardware not yet available

- Arduino-class microcontroller, preferably no general-purpose OS.
- Read-only Subaru SSM1 ECU polling after electrical/pinout verification.
- Additional dedicated sensor interfaces (e.g. AEM wideband AFR, boost).
- Timestamp, validate, normalize and locally log data.
- Send versioned telemetry messages to display over BLE or another agreed local wireless link.
- Never issue ECU writes or depend on the screen to protect the engine.

### 3.4 Bluetooth coexistence

The phone must be able to retain its existing head-unit connection while connected to the display. Test real device behavior; don't assume simultaneous connections, profiles, or permissions work identically across phones. **Audio remains on head unit.** The display link carries metadata/status only.

## 4. Functional requirements and acceptance criteria

Priority codes: **P0** necessary for first useful phone-connected release; **P1** subsequent desired; **P2** future hardware/features. `TBD` means platform or product decision unresolved, not implemented.

| ID | Priority | Requirement | Acceptance criterion |
|---|---|---|---|
| FR-001 | P0 | Boot directly into custom GC8 UI | Cold boot shows GC8 interface, no generic launcher |
| FR-002 | P0 | Round-display-safe layout | All critical text/readouts visible within circular usable area |
| FR-003 | P0 | Explicit unavailable/stale state | No invented RPM/AFR/boost/coolant values; connection failures clearly indicated |
| FR-004 | P0 | Phone pairing and reconnection | Pair once; subsequent reconnect without repeated manual setup where platform allows |
| FR-005 | P0 | Connection state | Clearly show paired, connected, reconnecting, disconnected; UI stays responsive |
| FR-006 | P0 | Clock/timezone sync | Correct local time after sync; sensible fallback while disconnected |
| FR-007 | P0 | Head-unit coexistence | Phone's music/call audio continues on existing Bluetooth head unit during display connection |
| FR-008 | P1 | Now-playing metadata **display only** | Show song title, artist, playback status; optional artwork if feasible |
| FR-009 | P1 | No media controls | No play/pause, previous/next, seek or volume controls in display firmware |
| FR-010 | P1 | Navigation guidance | Show next maneuver, distance, road name, ETA where accessible via supported phone APIs |
| FR-011 | P1 | Call metadata **display only** | Show incoming call status/name/number only where permission/platform supports it |
| FR-012 | P1 | No call/audio controls | No answer/decline, mic, speakers, call audio, audio streaming or head-unit replacement |
| FR-013 | P1 | View navigation | Minimal touch/swipe between clock, navigation, media info and future telemetry |
| FR-014 | P1 | Distraction-aware presentation | Primary information readable at a glance; no required text entry while driving |
| FR-015 | P1 | Phone loss behavior | Disconnected indicator; stale nav/media info removed or clearly marked |
| FR-016 | P2 | Voice assistant shortcut | Optional invocation via phone-supported mechanism; no voice processing on display |
| FR-017 | P2 | Garage shortcut | Optional authenticated integration; no unsecured open command |
| FR-018 | P2 | Read-only SSM1 telemetry | Supported ECU values display with valid timestamps; no ECU writes |
| FR-019 | P2 | Independent sensor telemetry | AFR, boost, oil pressure/temp only if compatible verified sensor interfaces exist |
| FR-020 | P2 | Data logging | Local timestamped exportable telemetry logs on hidden controller |
| FR-021 | P2 | Diagnostics | Supported read-only ECU diagnostics with explicit limits/unsupported states |
| FR-022 | P1 | Versioned data protocol | Schema version, message type, timestamps, units and validity/status defined and documented |

### 4.1 Navigation details\n\n**Updated owner priority:** Navigation is the first phone feature. Full moving map is preferred for user experience, but an initial display with **turn arrow, distance and street name** is explicitly acceptable. Clock, media and other phone features should not block navigation work.\n\n**Implementation caution:** Google Maps does not expose unrestricted turn-by-turn state to companion apps. Android notification-listener approaches are permission-dependent and may omit details or change between Maps versions. Validate on the actual phone. A true moving map may require a separately licensed routing/map source, phone-side rendering and an efficient transfer strategy; do not promise live Google Maps mirroring or full map tiles on the ESP32 without a tested design.\n\n**Navigation acceptance path:**\n- [ ] Confirm preferred navigation app and Android version.\n- [ ] Prototype supported guidance extraction or navigation source on phone.\n- [ ] Deliver real next-turn arrow, distance and street name to Waveshare.\n- [ ] Validate reroute, app background, no active route and disconnect states.\n- [ ] Investigate full map only after reliable turn-by-turn works.\n

Desired fields: maneuver icon, distance to next maneuver, street/road name, ETA, remaining trip distance, active/inactive guidance state. **Not required:** full map tiles, rendering Google Maps inside the ESP32, or route planning on the display. Prefer simple guidance; investigate phone OS/API constraints before committing to a specific navigation provider. If Google Maps doesn't expose needed data, document a supported alternative and obtain approval rather than scraping notifications without discussing reliability.

### 4.2 Music details

Display song title, artist, playback state and optionally album/artwork if feasible over BLE bandwidth. No control surface. The phone/head unit remains authoritative for track changes and volume. Media screen must remain useful if artwork unavailable.

### 4.3 Call details

Caller identification and ringing/active status are **optional/conditional** on platform permission and APIs. The display must not show misleading information if blocked by the OS. No buttons to accept, decline or control calls.

### 4.4 Clock and offline mode

Show time without requiring the hidden controller. Sync from phone when available; define fallback and time drift behavior after testing board RTC capabilities. Phone disconnect should not prevent viewing the UI.

### 4.5 Future engine monitoring

Current four dashboard fields are placeholders, not live measurements. Stock EJ20K/SSM1 cannot be assumed to expose all desired data; wideband AFR under boost needs independent measurement. Gauge data should include source, units, freshness, validity and optional alarm thresholds. Safety warnings must never imply OEM-grade protection unless independently validated.

## 5. Nonfunctional requirements

**Performance:** Fast boot and smooth LVGL redraw. Establish measured boot-time and frame-rate targets after profiling, rather than inventing fixed numbers. UI should remain responsive during wireless reconnection.

**Reliability:** Survive power cycling, BLE dropouts and missing sensors; never block boot on a phone; watchdog/recovery behavior to be verified.

**Power/installation:** Stable automotive-rated power regulation, transient/reverse-polarity protection, safe shutdown strategy and low parked-car parasitic draw. No permanent wiring change without a reviewed plan.

**Privacy/security:** Explicit Bluetooth pairing, no public unauthenticated command channel, local processing by default, minimal phone permissions, no contact/music/navigation history uploaded by default, no secrets or device keys committed.

**Maintainability:** C++ modules with clear boundaries (UI, transport, data model, status, hardware BSP); GitHub source of truth; update README/build instructions and requirements checklist when behavior changes.

**Usability:** Dark, motorsport-inspired visuals; daylight/night legibility; legible fonts on circular 466×466 screen; large touch targets; no distracting animations while driving.

**Compatibility:** ESP-IDF v6.1 successfully built and flashed initial UI on physical board; future dependency upgrades must be validated. Exact smartphone OS/version, BLE behavior and head-unit model are still unconfirmed.

## 6. Exclusions

- No ECU tuning, reflashing or active vehicle control.
- No replacement for existing factory/Defi/mechanical gauges.
- No smartphone OS or full app store on display.
- No audio streaming, speaker, microphone or media playback on ESP32.
- No play/pause, next/previous, seek, volume or other media controls.
- No call answer/decline or head-unit audio routing changes.
- No assumption of full Google Maps SDK/turn-by-turn access.
- No fabricated sensor readings, fake successful pairing or unverified 'working' claims.
- No requirement to buy ECU/sensor hardware to finish the phone milestone.

## 7. Current verified state vs planned work

### Verified on physical device (reported by owner)

- [x] Windows VS Code + ESP-IDF development environment configured.
- [x] Board identified by marking and box as Waveshare **1.75C Rev 1.1**.
- [x] ESP32-S3 target selected and USB COM4 used during initial development.
- [x] Generic C++ starter firmware flashed and boot logs observed.
- [x] Repository `JohnathanKong/gc8-gauge-display` created; GitHub push permission confirmed through successful commits.
- [x] Custom Waveshare BSP + LVGL dashboard code committed.
- [x] Custom dashboard successfully built using **ESP-IDF v6.1**.
- [x] Custom dashboard flashed and visually worked on the Waveshare (owner confirmation).
- [x] `build.bat` and README build instructions committed.
- [x] Second board retained as factory-demo reference (plan; **not independently verified** that it remains untouched).

### Not yet implemented or not verified

- [ ] Phone model and Android OS version confirmed.
- [ ] BLE pairing, reconnection and metadata transport.
- [ ] Phone companion application.
- [ ] Real clock synchronization.
- [ ] Music metadata on Waveshare.
- [ ] Navigation guidance on Waveshare.
- [ ] Call metadata on Waveshare.
- [ ] Hidden vehicle controller, SSM1, AFR/boost sensors.
- [ ] Automotive power installation.
- [ ] Physical UI daylight/night usability testing.

## 8. Delivery plan and detailed implementation checklist

**Rule:** Work one meaningful step at a time with the owner; commit coherent changes; do not ask them to perform five unrelated setup tasks at once. For each milestone, document exact commands and expected results. Test before calling it done.

### Milestone 0 — Initial firmware (COMPLETE)

- [x] Initialize ESP-IDF C++ project for ESP32-S3.
- [x] Integrate Waveshare 1.75C board-support component and LVGL.
- [x] Draw branded GC8 dashboard with unavailable telemetry placeholders.
- [x] Compile on ESP-IDF v6.1.
- [x] Flash and verify display operation on hardware.
- [x] Store source and build instructions in GitHub.

### Milestone 1 — Android navigation integration discovery and protocol (NEXT)

- [x] Confirm primary platform: **Android**.\n- [ ] Ask exact Android phone model and OS version.
- [ ] Confirm current head unit connection and whether phone supports simultaneous BLE peripheral data connection.
- [ ] Confirm whether installing a small companion app on phone is acceptable.
- [ ] Identify platform-supported sources for time, now-playing metadata, navigation and call status. Document unsupported/restricted capabilities; do not promise Google Maps or caller data without evidence.
- [ ] Define message schema: version, type, timestamp, payload, freshness, error/disconnected state.
- [ ] Decide BLE roles (phone central vs peripheral) based on platform feasibility; document pairing and security.
- [ ] Create minimal BLE connection and disconnection status on ESP32.
- [ ] Verify actual connection on physical phone and display.
- [ ] Update README with repeatable phone test procedure.

### Milestone 2 — Clock and resilient phone link

- [ ] Send time/timezone from phone to ESP32.
- [ ] Display accurate time and define disconnected fallback.
- [ ] Auto-reconnect and show clear status.
- [ ] Test head unit audio continues uninterrupted.
- [ ] Test power cycle, phone Bluetooth off/on, out-of-range and recovery.
- [ ] Record test results and limitations.

### Milestone 3 — Now-playing **metadata only**

- [ ] Implement supported phone-side metadata acquisition.
- [ ] Transmit title, artist and playback status.
- [ ] Display with truncation/scroll behavior appropriate to round screen.
- [ ] Optionally investigate album artwork; defer if memory/bandwidth costly.
- [ ] Verify playback and volume remain controlled solely by phone/head unit.
- [ ] Test stopped/paused/unknown metadata and disconnect behavior.

### Milestone 4 — Navigation

- [ ] Choose feasible navigation data source for selected phone OS.
- [ ] Define maneuver/ETA/distance messages with units and stale-state rules.
- [ ] Render glanceable turn guidance.
- [ ] Test start, reroute, arrival, app-background and disconnection scenarios.
- [ ] Document provider/API limitations honestly.

### Milestone 5 — Calls and optional conveniences

- [ ] Investigate permission-compliant incoming call metadata.
- [ ] If feasible, display incoming caller/status **without call controls**.
- [ ] If unavailable, document and defer rather than hack around OS protections.
- [ ] Consider assistant shortcut only after platform feasibility confirmed.
- [ ] Consider garage shortcut only with authenticated integration.

### Milestone 6 — Future vehicle controller

- [ ] Choose hidden microcontroller and electrical architecture.
- [ ] Verify GC8 EJ20K SSM1 interface pinout, signaling and read-only protocol.
- [ ] Bench test with current-limited/protected interface before connecting ECU.
- [ ] Integrate independent wideband AFR and other sensors after exact compatibility checks.
- [ ] Define controller-to-display telemetry protocol and freshness rules.
- [ ] Implement local logging/export on controller.
- [ ] Verify no effect on OEM ECU or driving behavior.

### Milestone 7 — Installation and hardening

- [ ] Design automotive power protection and sleep behavior.
- [ ] Validate safe placement, cable routing and physical mounting.
- [ ] Test heat/cold, brightness, touch reliability, ignition cycles and parked drain.
- [ ] Document recovery/firmware update process.
- [ ] Freeze a known-good release and tag it.

## 9. Developer workflow

1. Read this document and `README.md`.
2. Check the latest Git state and open issues; do not assume a prior chat's uncommitted code exists.
3. Make the smallest coherent change, commit to the repository, and state whether it was merely compiled or physically tested.
4. Keep `README.md` current with exact Windows VS Code ESP-IDF steps. `build.bat` requires **ESP-IDF: Open ESP-IDF Terminal**, not ordinary PowerShell.
5. Build with `build.bat`; only flash after successful compilation and configuration verification.
6. Update the checklist and decision log after each confirmed milestone.
7. Preserve head-unit audio routing and do not add media controls without an explicit requirement change.

## 10. Decisions log

| Date | Decision | Reason |
|---|---|---|
| 2026-10-21 | Target 1.75C Rev 1.1 | Marked on PCB and packaging |
| 2026-10-21 | ESP-IDF C++ / LVGL custom firmware | Fast, single-purpose embedded interface |
| 2026-10-21 | Display-only ESP32, hidden future acquisition controller | Separation of UI and vehicle safety/data responsibilities |
| 2026-10-21 | Prioritize phone before vehicle telemetry | ECU reader and sensor hardware not available yet |
| 2026-10-21 | Existing Bluetooth head unit retains all audio and controls | Avoid redundant UI and hardware |
| 2026-10-21 | Media/call metadata only; **no playback/call controls** | Explicit owner requirement |
| 2026-10-21 | Use placeholders until real sensors are connected | Avoid false readings |
| 2026-10-21 | Maintain detailed repo BRD and checklist | Enable continuation across chats/agents |

## 11. Open questions — do not guess

1. Primary smartphone is **Android**; exact model/OS version remains TBD.
2. Is installing a minimal companion app acceptable?
3. Preferred navigation application (Google Maps, Waze, Apple Maps, etc.)?
4. Is caller info actually wanted if it requires sensitive phone permissions?
5. Should the display communicate independently with both phone and hidden controller, or should one act as a gateway? Decide after BLE feasibility testing.
6. Final UI aesthetic and view navigation (to be iterated after working phone data).

## 12. New-chat handoff prompt

Copy/paste the following into a fresh chat:

> Continue my GC8 Digital Companion project in `JohnathanKong/gc8-gauge-display`. First read `BUSINESS_REQUIREMENTS.md`, `README.md`, and the actual code/commits. Treat the requirements and checklist as the scope and status source of truth. The Waveshare 1.75C Rev 1.1 custom LVGL dashboard already builds and runs on hardware with ESP-IDF v6.1. We're prioritizing phone connectivity before ECU/sensors. My existing Bluetooth head unit handles all audio, track controls and calls; the Waveshare displays metadata only, with no media or call controls. Identify the first unchecked item in the active milestone, ask only for genuinely missing information, then implement one tested step at a time. Commit code and update the docs; never claim hardware validation without my confirmation.

---
**Maintenance rule:** Every significant product decision updates this document. Every verified feature updates the relevant checkbox and README. Future agents must not treat unchecked features as implemented.
