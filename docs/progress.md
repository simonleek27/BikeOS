# Development progress

Updated: 25 November 2026

| Feature | Status | Evidence / next action |
|---|---|---|
| ESP32 identification | Complete | ESP32-S3 rev 0.2, 16 MB flash, 8 MB PSRAM, native USB verified |
| ESP-IDF bring-up | Complete | ESP-IDF v5.5.6 firmware built and flashed to the verified ESP32-S3; boot log captured |
| Shared C++ core | Complete for current demo | Strict host build and test suite pass |
| Real OSM map pipeline | Amsterdam BTIL city archive integrated | 1,115 CRC-checked 500 m tiles (~3.86 MB) in a separate 12 MB flash partition; ESP32 keeps a rider-centered 3×3 PSRAM window |
| Navigation engine | Complete for demo | Matching, maneuvers, off-route, reroute throttle, GPS loss, arrival |
| Virtual color screen bench | Ready | Standalone macOS/browser bench uses the production ColorMapRenderer, supports route planning, fault toggles, ride controls, and RGB565 screenshots |
| ESP navigation runtime | Complete for simulated source | Combined firmware builds; physical board still has the previous image until the new app + map partition are flashed |
| UART GPS ingestion | Implemented, not physically tested | Needs verified GPS module and ESP RX/TX wiring |
| ILI9341 TFT identification | Complete | 2.8-inch 240×320 SPI color module; color-bar test previously verified |
| ILI9341 SPI driver | Complete | 320×240 landscape RGB565 driver initialized at 26 MHz on the physical board |
| TFT wiring | Verified for current module | CS 10, RESET 4, DC 5, MOSI 11, SCK 12; power safety remains documented |
| TFT connection/ETA UX | Implemented and host-tested | Disconnected TFT shows BikeOS / Waiting for connection; phone sends route arrival clock via protocol message 18 and TFT renders `Arrive HH:MM` |
| BLE route transfer | Implemented and tested | ACK-paced transfer, 20/23/244/247-byte ATT fragmentation, strict reassembly, BRTE v1/v2 route-only bundles, transfer diagnostics, and activated-route verification |
| iPhone app | Navigation/planner redesign implemented | Compact map-first navigation layout, authoritative run-state controls, robust hit targets, real destination search, GraphHopper bicycle routing, route preview, custom BRTE upload, and active-route gating |
| Indoor simulator controls | Complete | Deterministic 4.5 m/s simulator starts only after Start, pauses at the exact position with speed 0, resumes, restarts, stops, reports run state over BLE, and restores a saved offset paused after reboot |
| Ride checkpoint recovery | Implemented and built | iPhone persists route/progress/coordinate; after reconnect it re-sends the saved route if needed and restores the ESP32 at the saved offset without auto-starting |
| iPhone heading/HUD | Implemented and simulator-built | ESP32 heading → iPhone course/compass → route bearing fallback; fixed 44-point controls and telemetry-stable HUD |
| iPhone launch flow | Implemented and simulator-built | Connect-first landing screen; planner appears after the ESP32 link is established |

## Current assessment

The custom-route planner now loads the GraphHopper endpoint correctly from the
local Xcode configuration. A literal `https://` in an `.xcconfig` was previously
parsed as a comment and became `https:`, which made MapKit search work while
route calculation failed. The planner also no longer hides routing errors behind
the bundled demo route and starts iPhone location updates when Home appears.
Navigation controls now remain disabled until the ESP32 status stream reports the
same route ID that the phone previewed; this prevents a failed or incomplete
custom transfer from accidentally starting the bundled demo route.

The data pipeline, navigation behavior, shared RGB565 renderer, virtual bench,
combined BTIL city-map backend, physical ESP execution, ILI9341 driver, and
first real iPhone↔ESP32 BLE connection are working. Transfer progress is driven
by acknowledgements rather than a blind write burst. GPS is currently the
deterministic scripted source; no physical GPS module is connected. Custom routes
are encoded into BRTE v1 or v2 and sent as route-only bundles; the ESP32 validates
that every route point is covered by the installed BTIL archive before activation.
BRTE v2 supports longer routes, and the ESP32 reloads map tiles around the actual
rider position, including while off-route.
The iPhone also has automatic reconnect and CoreBluetooth state restoration;
physical cycling reliability still needs to be exercised.

Firmware 0.5.0 and the 12 MB city-map partition were flashed to the physical
board on 25 November 2026; the boot log reported `City map: 1115 tiles`,
`OSM map ready (city tiles)`, and `ILI9341 initialized`. The checkpoint/heading
changes are built and ready for the next physical flash and reboot-resume test.
The ETA and connect-screen changes are host-tested and simulator-built; they
still need a physical firmware flash and iPhone run.

## Next priorities

1. Flash the current firmware and run the new connect/ETA screen on the TFT:
   confirm `BikeOS / Waiting for connection` while the phone is absent, then
   `Arrive HH:MM` after a route is sent.
2. Repeat the power-cycle recovery:
   confirm the saved route returns paused at the saved offset and **Resume**
   continues from there.
3. Repeat custom-route transfer on the physical board and capture commit ACK +
   activated route status in the serial log.
4. Run the physical Start/Pause/Resume/Stop/Restart and BT disconnect/reconnect
   sequence on the ILI9341 and iPhone together.
5. Connect and verify a physical GPS module, then replace deterministic GPS
   simulation for field testing.

Milestone 1—working firmware on the physical ESP32—is complete. Milestone 2a—
physical iPhone↔ESP32 BLE discovery and connection—is complete. Milestone 2b—
ILI9341 driver initialization and prior color-bar output—is complete; final
visual confirmation after the combined flash remains pending. Physical GPS
testing remains blocked on a verified GPS module and wiring.
## 2026-11-17 — BLE and iPhone milestone

- Added shared version-1 BikeOS BLE framing, CRC32 validation, bundle transfer metadata, acknowledgements, status, and reroute events.
- Built and flashed the ESP32-S3 BLE peripheral. Serial verification shows NimBLE initialized and `Advertising as BikeOS`.
- Added a real SwiftUI/CoreBluetooth Xcode project under `ios/` with a simulator-safe Amsterdam route preview, protocol implementation, demo transfer flow, and GraphHopper adapter configuration example.
- Physical iPhone radio validation is complete: the app discovered the ESP32,
  connected, and the ESP32 serial log recorded `BLE connected handle=1` with
  navigation status notifications. The final demo-route commit ACK still needs
  a captured serial trace.
