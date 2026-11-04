# BikeOS architecture (v1)

> Status: agreed direction as of 4 November 2026 (ND and Simon). Supersedes section 2 of `brainstorming.md`.
> Both bikes are non-electric Swapfiets bikes, so there is no e-bike battery or dynamo to integrate with for v1.

## The one-line version

The phone computes the route. The ESP has its own GPS and follows the route on its own. If the rider goes off route, the ESP asks the phone for a new one.

## Why not "phone is the navigation brain"

The original plan (phone runs GPS, routing and off-route detection; ESP is a BLE display) is simpler to write but has two costs we don't want to pay:

1. **Phone battery.** The phone must keep CoreLocation running in the background for the whole ride, roughly 5 to 10 percent per hour with the screen off, and the BLE link carries a stream of updates at 1 Hz.
2. **iOS background fragility.** A background location session that dies (low power mode, a force-quit, an OS kill) silently stops navigation. The rider only finds out at the next turn.

Moving GPS to the ESP removes both. The phone's only background job is answering an occasional reroute request over BLE, which costs almost nothing. A GPS module adds about €10 and 25 mA.

## Components and responsibilities

### iPhone app (SwiftUI, CoreBluetooth)

- Destination search and route preview.
- Route computation through a bike-profile routing engine over OpenStreetMap. **Not** MapKit cycling directions: Apple's bike routing was the thing that prompted this project. Start with a hosted GraphHopper (bike profile) call; later option is an embedded Valhalla or BRouter with an offline Netherlands extract.
- Send the full route to the ESP once at the start of the ride.
- Stay in BLE-only background mode (`bluetooth-central` background mode with state restoration). No location permission is required in v1.
- On a reroute request from the ESP: recompute from the supplied fix and heading, push the new route.

### ESP32-S3 firmware (ESP-IDF, LVGL)

- GPS module on UART (u-blox NEO-M8/M9 class, or any module with 1 Hz NMEA output).
- Route following: match each fix to the nearest segment of the route polyline, read distance to the next maneuver from a precomputed cumulative-distance table, render arrow, distance, street name, speed.
- Off-route detection: more than ~30 m from the line for 5 consecutive fixes, or the matched index moving backwards. Send one reroute request with fix and heading, then at most one every 10 to 15 s until a new route arrives.
- Fallback while waiting: point the arrow at the nearest point on the existing route. In a city this gets the rider back on track most of the time without any help from the phone.
- If no reroute arrives (phone asleep, app killed, out of range): show a "no phone" state. Never stay silently on a stale route.
- Physical buttons: page/select plus **left and right turn-signal buttons** (ND's addition; see open questions).

### Route payload (phone to ESP)

Precompute everything on the phone so the ESP does no geometry beyond nearest-segment search.

- Polyline: `int32 lat, int32 lon` in 1e-7 degrees, 8 bytes per point. A 5 km city route is a few hundred points, a few KB total.
- Cumulative distance per point (`uint32` metres), so distance-to-turn is a table lookup.
- Maneuver list: polyline index, turn type enum, street name string.
- Header: protocol version, route id, point count, maneuver count, total distance.
- Chunked over BLE within the negotiated MTU with sequence numbers; ESP acks the complete route by id before switching to it.

### Reroute request (ESP to phone)

`route_id, lat, lon, heading_deg, speed_kmh, timestamp` on a notify characteristic. Phone replies with a new full route (new route id).

## Power budget

- ESP32-S3 idle on BLE: ~30 mA. GPS module: ~25 mA. Display: depends entirely on the panel choice.
- The Waveshare 4.3B at ~2.25 W (5 V, 450 mA) dominates everything. A 10,000 mAh bank (~37 Wh) still gives 12 to 15 hours of riding, so **dynamo charging is not needed for v1** and stays an optional later milestone.
- A Sharp memory LCD (2.7", 400x240, LS027B7DH01) would drop the display to under 1 mA and is readable in direct sun with no backlight. It is monochrome and rules out a map, but it makes the whole device run for weeks on a single 18650. Worth a side-by-side outdoor test against the 4.3B before CAD, because the display decides the enclosure, the power source and the daylight readability all at once.

## Open questions

1. **Display:** 4.3B colour TFT (map possible, 2.25 W, borderline in sun at 550 nits) or memory LCD (arrow only, near-zero power, perfect in sun)?
2. **Turn-signal lights:** a signal on the handlebar unit faces forward and nobody behind can see it. Needs a rear unit (second ESP or a wire down the frame) or it is not worth building.
3. **Buttons on the 4.3B:** GPIO8/9 are the I2C bus for touch and the CH422G expander, so five buttons (page, select, left, right, plus one spare) probably need an I2C expander. Check the schematic before wiring anything.
4. **Mount:** quick-release and pocketable. The bikes are locked outside in Amsterdam and go back to Swapfiets eventually.
5. **Arrival and cancel:** how does the rider end a route from the bike without touching the phone?

## Build sequence (revised)

1. ESP + display bring-up: hello screen, buttons.
2. GPS bring-up: fixes on screen, speed.
3. Route following on a hard-coded route loaded over serial.
4. BLE: phone sends a route, ESP acks, ESP follows it.
5. Reroute round trip with the phone locked in a pocket.
6. Enclosure and mount, outdoor readability test, rain test.
7. Turn signals, if question 2 gets a good answer.
