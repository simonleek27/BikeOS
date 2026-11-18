# Development progress

Updated: 18 November 2026

| Feature | Status | Evidence / next action |
|---|---|---|
| ESP32 identification | Complete | ESP32-S3 rev 0.2, 16 MB flash, 8 MB PSRAM, native USB verified |
| ESP-IDF bring-up | Complete | v0.1.0 startup firmware built, flashed, and monitored |
| Shared C++ core | Complete for current demo | Strict host build and test suite pass |
| Real OSM map pipeline | Complete for bounded demo | BMAP v3 with deduplicated street-name table; 1,017 roads / 2,936 points, reproducible binary assets |
| Navigation engine | Complete for demo | Matching, maneuvers, off-route, reroute throttle, GPS loss, arrival |
| Native fake screen | Readable redesign complete | Exact 128×64 logical SDL surface with nearest-neighbor resize, reserved instruction/footer bands, 5×7 labels, selective roads, route halo/emphasis, large rider marker, automatic zoom hysteresis/smoothing, and native/4×/8× snapshots. |
| ESP navigation runtime | Complete for simulated source | Physical board completed 1.41 km scripted ride over serial |
| UART GPS ingestion | Implemented, not physically tested | Needs verified GPS module and ESP RX GPIO |
| LCD identification | Complete | ERC12864SBSF-14, ST7565R, 128 × 64, 30-pin 0.5 mm FPC |
| LCD SPI driver | Implemented, not physically tested | Needs bottom-contact carrier and charge-pump capacitor network |
| LCD wiring | Blocked on physical carrier | Provide adapter/carrier and clear ESP board photos; then assign GPIOs |
| BLE route transfer | ACK-driven implementation | iPhone now waits for begin/chunk/commit ACKs, sends one bounded chunk at a time, validates transfer/bundle IDs, and verifies the activated route ID from status; physical repeat is the next verification |
| iPhone app | Physical-demo ready | SwiftUI now decodes the bundled BRTE route (points + street maneuvers), renders the full route, shows live ESP32 position/status, and exposes transfer progress; GraphHopper response parsing is implemented but needs an API key |

## Current assessment

The data pipeline, navigation behavior, shared framebuffer, simulator, physical
ESP execution, and the first real iPhone↔ESP32 BLE connection are working. The
iPhone and ESP32 now share the same validated BRTE route metadata, and transfer
progress is driven by acknowledgements rather than a blind write burst. The
LCD is still not physically wired, so pixel output remains simulator/
firmware-framebuffer verified only. GPS is still the deterministic scripted
source; no physical GPS module is connected.

## Next priorities

1. Repeat the demo-route transfer on the physical board and capture the final
   commit ACK plus activated route status in the serial log.
2. Build the safe LCD FPC carrier circuit, verify the module, assign GPIOs, and
   run the physical all-pixels/text/map sequence.
3. Connect and verify a physical GPS module, then replace deterministic GPS
   simulation for field testing.

Milestone 1—working firmware on the physical ESP32—is complete. Milestone 2a—
physical iPhone↔ESP32 BLE discovery and connection—is complete. Physical LCD
testing remains blocked on the carrier hardware; physical GPS testing remains
blocked on a verified GPS module and wiring.
## 2026-11-17 — BLE and iPhone milestone

- Added shared version-1 BikeOS BLE framing, CRC32 validation, bundle transfer metadata, acknowledgements, status, and reroute events.
- Built and flashed the ESP32-S3 BLE peripheral. Serial verification shows NimBLE initialized and `Advertising as BikeOS`.
- Added a real SwiftUI/CoreBluetooth Xcode project under `ios/` with a simulator-safe Amsterdam route preview, protocol implementation, demo transfer flow, and GraphHopper adapter configuration example.
- Physical iPhone radio validation is complete: the app discovered the ESP32,
  connected, and the ESP32 serial log recorded `BLE connected handle=1` with
  navigation status notifications. The final demo-route commit ACK still needs
  a captured serial trace.
