# Development progress

Updated: 16 November 2026

| Feature | Status | Evidence / next action |
|---|---|---|
| ESP32 identification | Complete | ESP32-S3 rev 0.2, 16 MB flash, 8 MB PSRAM, native USB verified |
| ESP-IDF bring-up | Complete | v0.1.0 startup firmware built, flashed, and monitored |
| Shared C++ core | Complete for current demo | Strict host build and test suite pass |
| Real OSM map pipeline | Complete for bounded demo | 1,017 roads / 2,936 points, reproducible binary assets |
| Navigation engine | Complete for demo | Matching, maneuvers, off-route, reroute throttle, GPS loss, arrival |
| Native fake screen | Functional, **not visually usable yet** | SDL simulator runs and produces deterministic frames, but the 128×64 view is too dense and status text/position marker are hard to read. Redesign is the top software priority. |
| ESP navigation runtime | Complete for simulated source | Physical board completed 1.41 km scripted ride over serial |
| UART GPS ingestion | Implemented, not physically tested | Needs verified GPS module and ESP RX GPIO |
| LCD identification | Complete | ERC12864SBSF-14, ST7565R, 128 × 64, 30-pin 0.5 mm FPC |
| LCD SPI driver | Implemented, not physically tested | Needs bottom-contact carrier and charge-pump capacitor network |
| LCD wiring | Blocked on physical carrier | Provide adapter/carrier and clear ESP board photos; then assign GPIOs |
| BLE route transfer | Not started | Version payload/chunk/ack protocol next |
| iPhone app | Not started | SwiftUI destination/route/BLE work follows protocol |

## Current assessment

The data pipeline, navigation behavior, shared framebuffer, simulator, and
physical ESP execution are working. The current screen should **not** be treated
as a finished interface: user review found it effectively impossible to read at
normal size. Too many street segments compete with the route, the rider marker
is too small, and the 3×5 status text does not establish a clear visual
hierarchy.

## Next priorities

1. Redesign the 128×64 presentation before adding more features: filter minor
   roads by zoom, simplify geometry more aggressively for display, enlarge the
   route and rider marker, and reserve a clean maneuver/status region.
2. Define visual acceptance screenshots at actual size and 4× scale for
   `NAVIGATING`, `REROUTING`, `GPS_LOST`, and `ARRIVED`.
3. Build the safe LCD FPC carrier circuit, assign verified ESP GPIOs, and run
   the physical all-pixels/text/map sequence.
4. Implement BLE bundle transfer and the iPhone app after the on-screen design
   is readable.

Milestone 1—working firmware on the physical ESP32—is complete. Physical LCD
testing remains blocked on the carrier hardware. The immediate independent
software task is the display redesign described above.
