# Development progress

Updated: 17 November 2026

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
| BLE route transfer | Not started | Version payload/chunk/ack protocol next |
| iPhone app | Not started | SwiftUI destination/route/BLE work follows protocol |

## Current assessment

The data pipeline, navigation behavior, shared framebuffer, simulator, and
physical ESP execution are working. The redesigned screen now gives the
maneuver and distance a fixed hierarchy while preserving real geographic
geometry. The LCD is still not physically wired, so pixel output remains
simulator/firmware-framebuffer verified only.

## Next priorities

1. Build the safe LCD FPC carrier circuit, assign verified ESP GPIOs, and run
   the physical all-pixels/text/map sequence.
2. Implement BLE bundle transfer and the iPhone app after the on-screen design
   is readable.

Milestone 1—working firmware on the physical ESP32—is complete. Physical LCD
testing remains blocked on the carrier hardware. The immediate independent
software task is the display redesign described above.
## 2026-11-17 — BLE and iPhone milestone

- Added shared version-1 BikeOS BLE framing, CRC32 validation, bundle transfer metadata, acknowledgements, status, and reroute events.
- Built and flashed the ESP32-S3 BLE peripheral. Serial verification shows NimBLE initialized and `Advertising as BikeOS`.
- Added a real SwiftUI/CoreBluetooth Xcode project under `ios/` with a simulator-safe Amsterdam route preview, protocol implementation, demo transfer flow, and GraphHopper adapter configuration example.
- Real iPhone radio validation is still pending: use `docs/ble-ios.md` and report the nRF Connect result before calling that milestone complete.
