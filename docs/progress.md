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
| BLE route transfer | Demo path implemented; physical link verified | ESP32 advertises the corrected service UUID; physical iPhone connected and received status notifications; repeat the demo transfer with serial capture to verify the final commit ACK |
| iPhone app | Working physical demo | SwiftUI map preview, CoreBluetooth discovery, status notifications, and demo route transfer install/run on a physical iPhone; production UX and ACK-driven retry/windowing remain |

## Current assessment

The data pipeline, navigation behavior, shared framebuffer, simulator, physical
ESP execution, and the first real iPhone↔ESP32 BLE connection are working. The
redesigned screen now gives the maneuver and distance a fixed hierarchy while
preserving real geographic geometry. The LCD is still not physically wired, so
pixel output remains simulator/firmware-framebuffer verified only. GPS is still
the deterministic scripted source; no physical GPS module is connected.

## Next priorities

1. Capture and validate a complete demo-route transfer ACK on the ESP32, then
   add ACK-driven retry/windowing instead of the current simple demo stream.
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
