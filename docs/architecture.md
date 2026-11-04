# BikeOS architecture (v1)

> Status: agreed direction as of 4 November 2026 (ND and Simon). Supersedes section 2 of `brainstorming.md`.
> Both bikes are non-electric Swapfiets bikes, so there is no e-bike battery or dynamo to integrate with for v1.

## The one-line version

The phone computes the route. The ESP has its own GPS and follows the route on its own. If the rider goes off route, the ESP asks the phone for a new one.

## Scope for v1

In: ESP32-S3, memory LCD, GPS module, front light, BLE link, iPhone app, route following and rerouting.

Out, deliberately: turn signals (buttons get fitted but not wired), handlebar mount and enclosure (mechanical track, separate), speed display, offline routing, dynamo charging, maps on screen.

## Why not "phone is the navigation brain"

The original plan (phone runs GPS, routing and off-route detection; ESP is a BLE display) is simpler to write but has two costs we don't want to pay:

1. **Phone battery.** The phone must keep CoreLocation running in the background for the whole ride, roughly 5 to 10 percent per hour with the screen off, and the BLE link carries a stream of updates at 1 Hz.
2. **iOS background fragility.** A background location session that dies (low power mode, a force-quit, an OS kill) silently stops navigation. The rider only finds out at the next turn.

Moving GPS to the ESP removes both. The phone's only background job is answering an occasional reroute request over BLE, which costs almost nothing.

## Hardware (decided 2026-11-04)

| Part | Pick | Why | Where |
|---|---|---|---|
| MCU | ESP32-S3, 8 MB flash / 8 MB PSRAM (DevKitC-1 N8R8 to prototype; a board with LiPo charging such as Seeed XIAO ESP32S3 or Adafruit Feather ESP32-S3 for the bike) | BLE 5, enough RAM for a framebuffer and route, cheap | TinyTronics ~€18 |
| Display | **Adafruit SHARP Memory Display Breakout 2.7" 400x240** (product 4694, Sharp LS027B7DH01) | Reflective, so readable in direct sun with no backlight; draws under 1 mA; 3-wire SPI; breakout handles the 0.5 mm flex and supply; viewable 58.8 x 35.3 mm | Opencircuit (NL) €61.50, 10 to 12 days; The Pi Hut £43 plus UK import |
| Display, bigger option later | Sharp LS044Q7DH01 4.4" 320x240, viewable 90.8 x 68.4 mm | Same tech, larger; no breakout exists, bare panel plus own flex adapter | DigiKey ~€55 |
| Front light | Two small white LEDs in the top bezel angled across the glass, or an edge-lit acrylic strip, on a GPIO | Memory LCDs have no backlight and the transmissive fraction (0.35 percent) is useless for one; Amsterdam is dark by 17:00 in winter | Any |
| GPS | **GY-GPSV3-NEO-M8N** (u-blox NEO-M8N, 25 mm patch antenna, backup battery, UART 9600 NMEA) | Multi-constellation helps between tall buildings; backup battery keeps clock and ephemeris so the next start takes seconds, which is how "remembers where it was parked" works; ~25 mA | TinyTronics ~€25 to €30, listed 2 to 4 weeks. Do not buy from eBay or AliExpress, fake M8Ns are common |
| GPS, fallback | Adafruit Ultimate GPS (MTK3333) | Known good, battery holder, similar price | Opencircuit or Kiwi |
| Battery | Single 18650 (3000 mAh) or a 2000 to 3000 mAh LiPo with the board's charger | ~70 mA total draw gives roughly 40 hours of riding | Any |
| Buttons | Fit three momentary switches (page, select, spare) plus two for turn signals. Turn-signal pair not wired in v1 | Keeps the enclosure from needing a redesign later | Muco |

Power budget: ESP32-S3 on BLE 30 to 40 mA, GPS 25 mA, display under 1 mA, front light a few mA when on. The 4.3" colour TFT and the USB power bank from the original plan are dropped.

## Components and responsibilities

### iPhone app (SwiftUI, CoreBluetooth)

- Destination search and route preview.
- Route computation through a bike-profile routing engine over OpenStreetMap. **Not** MapKit cycling directions: Apple's bike routing was the thing that prompted this project. Hosted GraphHopper (bike profile) call. Needs a data connection at the start of a ride and for each reroute; accepted for v1.
- Send the full route to the ESP once at the start of the ride.
- Stay in BLE-only background mode (`bluetooth-central` background mode with state restoration). No location permission is required in v1.
- On a reroute request from the ESP: recompute from the supplied fix and heading, push the new route.
- Ending or cancelling a route happens in the app. The device has no "end ride" button in v1; the phone sends a clear-route command.

### ESP32-S3 firmware (ESP-IDF, LVGL 1-bit)

- GPS on UART at 1 Hz.
- Route following: match each fix to the nearest segment of the route polyline, read distance to the next maneuver from a precomputed cumulative-distance table, render arrow, distance, street name.
- Off-route detection: more than ~30 m from the line for 5 consecutive fixes, or the matched index moving backwards. Send one reroute request with fix and heading, then at most one every 10 to 15 s until a new route arrives.
- Fallback while waiting: point the arrow at the nearest point on the existing route. In a city this gets the rider back on track most of the time without any help from the phone.
- If no reroute arrives (phone asleep, app killed, out of range): show a "no phone" state. Never stay silently on a stale route.
- Front light on a GPIO, toggled by a button or a light sensor later.
- Persist last fix to flash on power-down so the start screen can show "near last position" before the GPS locks.

### Route payload (phone to ESP)

Precompute everything on the phone so the ESP does no geometry beyond nearest-segment search.

- Polyline: `int32 lat, int32 lon` in 1e-7 degrees, 8 bytes per point. A 5 km city route is a few hundred points, a few KB total.
- Cumulative distance per point (`uint32` metres), so distance-to-turn is a table lookup.
- Maneuver list: polyline index, turn type enum, street name string.
- Header: protocol version, route id, point count, maneuver count, total distance.
- Chunked over BLE within the negotiated MTU with sequence numbers; ESP acks the complete route by id before switching to it.
- This format moves to `protocol/` as a versioned spec before either side writes code.

### Reroute request (ESP to phone)

`route_id, lat, lon, heading_deg, speed_kmh, timestamp` on a notify characteristic. Phone replies with a new full route (new route id).

## Open questions

1. **Which Swapfiets model** (bar diameter, stem shape). Blocks the mechanical track, not the electronics.
2. **Waterproofing** is a mechanical problem: fully sealed enclosure, sealed buttons, sealed charge port. Owned by the enclosure work, not v1 firmware.
3. **Turn signals**: a handlebar blinker faces forward; needs a rear unit to be useful. Parked.
4. **Budget**: display plus GPS is roughly €90, offset by dropping the 4.3B (€37.50) and the power bank (€12).

## Build sequence

1. ESP + memory LCD bring-up: hello screen, buttons, front light.
2. GPS bring-up: fixes on screen, time to first fix cold and warm.
3. Route following on a hard-coded route loaded over serial.
4. BLE: phone sends a route, ESP acks, ESP follows it.
5. Reroute round trip with the phone locked in a pocket.
6. Outdoor readability test in sun and at night, then hand off to the mechanical track for the enclosure.
