# BikeOS architecture (current v1)

Updated 24 November 2026. The current prototype uses the verified 2.8-inch
ILI9341 color TFT; the earlier ST7565R/LCD design is historical and is not the
physical test target.

## One-line version

The iPhone selects or calculates a bicycle route. The ESP32-S3 receives a
validated BRTE bundle over BLE, follows it from GPS, renders a heading-up map
from its stored BMAP, and reports authoritative status back to the phone.

## Hardware

| Part | Current choice | Status |
|---|---|---|
| MCU | ESP32-S3 rev 0.2, 16 MB flash, 8 MB PSRAM | Verified on USB Serial/JTAG |
| Display | 2.8-inch ILI9341, 240×320 RGB565 SPI TFT | Driver initialized and color-bar test verified |
| TFT signals | CS 10, RESET 4, DC 5, MOSI 11, SCK 12 | Current firmware configuration |
| GPS | NEO-6M-class UART receiver, RX 18/TX 17, 9600 baud | Driver implemented; physical receiver not tested |
| Phone | SwiftUI iOS app, CoreBluetooth, MapKit | Builds and protocol tests pass |

TFT power depends on the module J1 and the carrier IN-OUT bridge. Follow
[`docs/hardware-setup.md`](hardware-setup.md) before changing power. Touch and
microSD are intentionally disconnected in v1.

## Responsibilities

### iPhone

- Search for a destination and optional manual start with MapKit.
- Request a real GraphHopper `bike` route and preview geometry/maneuvers.
- Encode the selected route as BRTE v1 and transfer it with ACK pacing.
- Present ESP32 status, map position, next maneuver, and the acknowledged run
  state. The phone does not run an independent ride animation.
- Recompute a route when a future reroute request is supported end-to-end.

### ESP32-S3

- Own the active route after transfer commit.
- Use deterministic simulated GPS indoors or UART NMEA GPS outdoors.
- Match fixes to the route, advance maneuvers, detect off-route/GPS loss, and
  report arrival.
- Render the stored map, route, rider, GPS status, and Bluetooth status to the
  ILI9341 RGB565 framebuffer.
- Advertise the version-1 BLE service and notify status/ACK/event frames.

### Shared C++ core

The same navigation, GPS filtering, route/map codecs, and color renderer are
used by the ESP-IDF firmware, host tests, and `host/virtual_screen` bench. This
keeps the screen bench meaningful instead of being a separate mock UI.

## Data flow

1. GraphHopper returns bicycle geometry and turn instructions.
2. Swift encodes the route as BRTE v1, including a route ID and CRC.
3. BLE sends `transfer_begin`, ACK-paced `transfer_chunk` frames, and
   `transfer_commit`.
4. Frames larger than the negotiated ATT write size use the version-1 fragment
   envelope; the ESP32 reassembles and CRC-validates the original frame before
   decoding it.
5. The ESP32 activates the route only after bundle and route CRC checks pass.
6. Status notifications carry GPS validity, phone connection, route progress,
   speed, heading, and the authoritative run state.

## Navigation run state

The ESP32 is authoritative for `not_started`, `running`, `paused`, `stopped`,
and `arrived`. The iPhone sends explicit Start/Pause/Resume/Stop/Restart
commands and updates controls only after the corresponding ACK/status. While
paused, the simulator stops advancing, reports zero speed, and continues
status notifications.

## Map and route limits

The checked-in BMAP covers a bounded Amsterdam area around Museumplein. Custom
routes are sent route-only, so the ESP32 keeps its stored map and draws the new
route without pretending unrelated surrounding roads exist. BRTE v1 uses signed
16-bit coordinate offsets from the first point; geometry outside that window is
rejected rather than truncated. Broader tiled offline coverage is future work.

## Test surfaces

- `ctest --test-dir build-host`: C++ unit/integration tests.
- `build-host/bikeos_simulator`: legacy monochrome host simulator and snapshots.
- `host/virtual_screen/bench.sh`: production 320×240 color-TFT bench with fault
  toggles, route planning, and ride controls.
- Xcode `BikeOSTests`: Swift frame, BRTE, status, ACK, and BLE-fragment tests.
- Physical ESP32: ESP-IDF build, flash, serial boot diagnostics, TFT, BLE, and
  GPS acceptance sequence in [`docs/test-plan.md`](test-plan.md).

## Deliberate non-goals for the first physical test

No music controls, weather, dynamo integration, touch-screen input, full-city
offline map pack, or phone-operated navigation while riding. The first outdoor
route must be short, secured to the bicycle, and tested without operating the
phone in motion.
