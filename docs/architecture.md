# BikeOS architecture (v1)

> Status: agreed direction as of 4 November 2026 (ND and Simon). Supersedes section 2 of `brainstorming.md`.
> Updated 17 November 2026: the display is the 128x64 ST7565R panel already on hand, replacing the Sharp memory LCD, and a map on the screen is in v1 scope (see "Display" and "Map on screen").
> Both bikes are non-electric Swapfiets bikes, so there is no e-bike battery or dynamo to integrate with for v1.
>
> Hardware state, 16 November 2026: the ESP32-S3 is verified with 16 MB flash and 8 MB PSRAM. The LCD is verified as an EastRising ERC12864SBSF-14 (ST7565R, 128x64); it is not wired yet because the bare panel needs its FPC carrier and charge-pump capacitor network.

## The one-line version

The phone computes the route. The ESP has its own GPS and follows the route on its own. If the rider goes off route, the ESP asks the phone for a new one. The ESP draws a heading-up map around the rider from a city map stored on the device, with the route on top, car-nav style.

## Scope for v1

In: ESP32-S3, ST7565R 128x64 LCD with its LED backlight, GPS module, BLE link, iPhone app, route following and rerouting, heading-up map on screen from a preloaded Amsterdam map.

Out, deliberately: turn signals (buttons get fitted but not wired), handlebar mount and enclosure (mechanical track, separate), speed display, full offline route calculation on the ESP, dynamo charging, map coverage outside the preloaded region.

## Why not "phone is the navigation brain"

The original plan (phone runs GPS, routing and off-route detection; ESP is a BLE display) is simpler to write but has two costs we don't want to pay:

1. **Phone battery.** The phone must keep CoreLocation running in the background for the whole ride, roughly 5 to 10 percent per hour with the screen off, and the BLE link carries a stream of updates at 1 Hz.
2. **iOS background fragility.** A background location session that dies (low power mode, a force-quit, an OS kill) silently stops navigation. The rider only finds out at the next turn.

Moving GPS to the ESP removes both. The phone's only background job is answering an occasional reroute request over BLE, which costs almost nothing.

## Hardware (decided 2026-11-04, display revised 2026-11-17)

| Part | Pick | Why | Where |
|---|---|---|---|
| MCU | ESP32-S3. The connected prototype measures **16 MB flash / 8 MB PSRAM**; exact module/carrier SKU awaits a silkscreen photo. A board with LiPo charging such as Seeed XIAO ESP32S3 or Adafruit Feather ESP32-S3 remains an option for the bike. | BLE 5, enough RAM for a framebuffer and route, cheap | Prototype physically on hand |
| Display | **EastRising ERC12864SBSF-14**, 128 × 64, ST7565R, 30-pin 0.5 mm FPC | Actual panel on hand; compact monochrome framebuffer; 4-wire serial supported | Prototype physically on hand; carrier circuit required |
| Backlight | Panel's separate white LED backlight through a current-limited transistor/MOSFET path | Approximately 45 mA typical is too much for a GPIO | On the display assembly |
| GPS | **GY-GPSV3-NEO-M8N** (u-blox NEO-M8N, 25 mm patch antenna, backup battery, UART 9600 NMEA) | Multi-constellation helps between tall buildings; backup battery keeps clock and ephemeris so the next start takes seconds, which is how "remembers where it was parked" works; ~25 mA | TinyTronics ~€25 to €30, listed 2 to 4 weeks. Do not buy from eBay or AliExpress, fake M8Ns are common |
| GPS, fallback | Adafruit Ultimate GPS (MTK3333) | Known good, battery holder, similar price | Opencircuit or Kiwi |
| Battery | Single 18650 (3000 mAh) or a 2000 to 3000 mAh LiPo with the board's charger | ~70 mA without backlight, ~110 mA with it: roughly 25 to 40 hours of riding | Any |
| Buttons | Fit three momentary switches (page, select, spare) plus two for turn signals. Turn-signal pair not wired in v1 | Keeps the enclosure from needing a redesign later | Muco |

Power budget: ESP32-S3 on BLE 30 to 40 mA, GPS 25 mA, LCD logic under 1 mA, backlight about 45 mA when on. Worst case with the backlight on all ride is roughly 110 mA, so a 3000 mAh cell gives about 25 hours; daytime riding with the backlight off stays near the original 40. Measure once the carrier and GPS are connected. The 4.3" colour TFT and the USB power bank from the original plan stay dropped.

### Display: why the ST7565R and not the Sharp memory LCD

The 4 November plan picked the Adafruit Sharp Memory Display 2.7" (400x240). Its only NL source quoted 10 to 12 days, so we bought the EastRising ERC12864SBSF-14 instead, and the firmware, renderer and simulator are now built around it. It is the v1 display.

What this costs, so nobody rediscovers it later:

- **Resolution.** 128x64 is about a twelfth of the Sharp's pixels, so the map has to be selective: route, rider and the nearby streets that matter for the next turn, not the whole street grid. The renderer's acceptance criteria in `docs/HANDOFF.md` are how we hold it to that.
- **Sunlight and night.** It is a transflective STN panel with an LED backlight instead of a reflective memory LCD with a front light. Readability in direct sun is untested.
- **Wiring.** The bare 30-pin 0.5 mm FPC needs a carrier and the charge-pump capacitor network before it can show a pixel (`docs/hardware-setup.md`).

Both decisions stand: this panel, and a map on it. The map is designed for 128x64 rather than dropped because of it.

## Map on screen (added 2026-11-17)

Turn arrows alone are not enough: Amsterdam street names are hard to read from the bike, so the rider needs to see the shape of the streets around them. The screen shows a heading-up map like a car nav, with the route drawn thick on top and the maneuver strip along the bottom.

**Decision: the map lives on the device; the phone still only sends the route.**

What exists today (`docs/map-data.md`):

- **Build step (laptop, `tools/prepare_amsterdam_demo.py`):** a bounded Overpass query for OSM road geometry, simplified, with road classes and a deduplicated street-name table.
- **Format:** `BMAP` v3, CRC-checked, coordinates as int16 offsets at 1e-6 degrees. The demo extract is 1,017 roads and 2,936 points in about 23 KB.
- **Storage:** embedded in the firmware image.
- **Rendering:** shared C++ renderer draws a heading-up map into the 1,024-byte ST7565 framebuffer, with top instruction and bottom maneuver bands, route halo and automatic zoom.

Still to do for full-city coverage:

- **Source:** switch from Overpass to a Geofabrik Noord-Holland extract.
- **Format:** split into a grid of roughly 500 m cells with an index header so the ESP loads only the cells around the fix. Measure the Amsterdam size; it decides flash versus microSD.
- **Updates:** the ESP pulls a new map file over WiFi at home, roughly monthly.
- **Transfer:** the iOS demo currently sends the `BMAP` over BLE with the route. Once the map is stored on the device, the phone sends only the route.

Why not stream map data from the phone:

1. BLE throughput on iOS is tens of KB/s in practice. Sending map data for every route and every reroute is slow and costs phone battery, which this architecture exists to avoid.
2. The map must keep working off route, which is exactly when the rider needs it. A corridor streamed around the old route goes blank once the rider leaves it.

Cost: coverage is limited to the preloaded region. Fine for two riders in Amsterdam. If coverage anywhere is ever needed, the fallback is the phone sending a simplified ~300 m corridor around the route; that is a v2 question.

## Performance and latency (added 2026-11-17)

Rendering and transfer are not the bottleneck. GPS is, and it needs handling in firmware or the map will feel bad.

Fine as designed:

- **Rendering:** a few thousand rotated segments into a 400x240 1-bit buffer takes a few ms on the S3. Draw the map straight into the framebuffer with our own line routine, not as LVGL line objects. LVGL only for the bottom strip.
- **Display refresh:** a full frame is about 12 KB. The Sharp panel runs SPI at roughly 1 to 2 MHz, so a full redraw is 50 to 100 ms, i.e. 10+ fps. Use DMA so the CPU is not blocked.
- **Map loading:** cells are tens of KB, read in ms from microSD. Prefetch neighbouring cells when approaching a cell edge. Put the SD card and the display on separate SPI hosts if possible.
- **Route transfer:** a few KB over BLE, under a second.

Needs handling:

- **1 Hz GPS makes the map step.** At 20 km/h that is ~5.5 m between fixes. Run the NEO-M8N at 5 Hz and interpolate position along the route between fixes so the map moves every frame. 9600 baud NMEA cannot carry 5 Hz: switch the module to UBX NAV-PVT only at 115200.
- **Heading is noise when slow or stopped.** GPS course over ground is unreliable below ~5 km/h (every red light). Freeze heading below a speed threshold. While on route, use the route bearing at the matched point instead of raw GPS heading, which also smooths turns.
- **Urban GPS error.** Expect 5 to 15 m of drift on narrow streets. Snap the rider marker to the route while inside the off-route threshold so it does not wander onto the parallel street or canal.
- **Reroute latency is ~6 to 10 s** (detection window, cellular round trip to the routing API, BLE push). Acceptable because the map keeps showing position meanwhile. Keep the off-route window time-based (~5 s), not fix-count based, so it does not shrink at 5 Hz.
- **Cold.** Liquid crystal responds more slowly near freezing, which is normal on Amsterdam winter mornings. Measure smearing in the outdoor test rather than assuming.

## Components and responsibilities

### iPhone app (SwiftUI, CoreBluetooth)

- Destination search and route preview.
- Route computation through a bike-profile routing engine over OpenStreetMap. **Not** MapKit cycling directions: Apple's bike routing was the thing that prompted this project. Hosted GraphHopper (bike profile) call. Needs a data connection at the start of a ride and for each reroute; accepted for v1.
- Send the full route to the ESP once at the start of the ride.
- Stay in BLE-only background mode (`bluetooth-central` background mode with state restoration). No location permission is required in v1.
- On a reroute request from the ESP: recompute from the supplied fix and heading, push the new route.
- Ending or cancelling a route happens in the app. The device has no "end ride" button in v1; the phone sends a clear-route command.

### ESP32-S3 firmware (ESP-IDF, shared C++ 1-bit renderer)

- GPS on UART: configure the NEO-M8N for UBX NAV-PVT only at 115200 baud, 5 Hz, with the same fix/filter path tested on the host. Interpolate position along the route between fixes; freeze heading below ~5 km/h and use route bearing while on route (see "Performance and latency").
- Decode a bounded local OSM geometry asset and render it into the exact
  ST7565 1,024-byte page framebuffer.
- Route following: match each fix to the nearest segment of the route polyline, read distance to the next maneuver from a precomputed cumulative-distance table, render arrow, distance, street name.
- Off-route detection: more than ~30 m from the line for ~5 s of fixes, or the matched index moving backwards. Send one reroute request with fix and heading, then at most one every 10 to 15 s until a new route arrives.
- Fallback while waiting: point the arrow at the nearest point on the existing route. In a city this gets the rider back on track most of the time without any help from the phone.
- If no reroute arrives (phone asleep, app killed, out of range): show a "no phone" state. Never stay silently on a stale route.
- Map rendering: heading-up map from the stored map file, route on top, maneuver arrow and distance in a bottom strip (see "Map on screen").
- Map update over WiFi when on a known network.
- Backlight on a GPIO through a transistor, toggled by a button or a light sensor later.
- Persist last fix to flash on power-down so the start screen can show "near last position" before the GPS locks.

### Route payload (phone to ESP)

Precompute everything on the phone so the ESP does no geometry beyond nearest-segment search.

- Polyline: `int32 lat, int32 lon` in 1e-7 degrees, 8 bytes per point. A 5 km city route is a few hundred points, a few KB total.
- Cumulative distance per point (`uint32` metres), so distance-to-turn is a table lookup.
- Maneuver list: polyline index, turn type enum, street name string.
- Header: protocol version, route id, point count, maneuver count, total distance.
- Chunked over BLE within the negotiated MTU with sequence numbers; ESP acks the complete route by id before switching to it.
- Implemented as protocol version 1 in `protocol/include/bikeos/protocol.hpp`, mirrored in `ios/BikeOS/Services/BikeOSProtocol.swift`.

### Reroute request (ESP to phone)

`route_id, lat, lon, heading_deg, speed_kmh, timestamp` on a notify characteristic. Phone replies with a new full route (new route id).

## Open questions

1. **Which Swapfiets model** (bar diameter, stem shape). Blocks the mechanical track, not the electronics.
2. **Waterproofing** is a mechanical problem: fully sealed enclosure, sealed buttons, sealed charge port. Owned by the enclosure work, not v1 firmware.
3. **Turn signals**: a handlebar blinker faces forward; needs a rear unit to be useful. Parked.
4. **Map storage**: onboard flash for the demo extract; microSD if the full Amsterdam grid does not fit.
5. **Map legibility on 128x64**: which streets and zoom levels make the shape of the junction readable at a glance. Settle it on the real panel outdoors, in sun and at night.

## Build sequence

1. ESP bring-up and physical flash. Done.
2. Shared navigation and map core, 128x64 simulator. Done.
3. LCD carrier, verified wiring, on-glass display test.
4. UART GPS bring-up: fixes on screen, time to first fix cold and warm, switch to UBX NAV-PVT at 115200 and 5 Hz.
5. BLE: phone sends a route, ESP acks, ESP follows it. Protocol and firmware advertising done; needs a real iPhone test.
6. Reroute round trip with the phone locked in a pocket.
7. Outdoor readability test in sun, at night, and near freezing (check for smearing), then hand off to the mechanical track for the enclosure.
