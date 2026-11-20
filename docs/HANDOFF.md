# BikeOS handoff

## Read this first

The navigation stack, physical ESP32 runtime, ILI9341 renderer, and the first
physical iPhone↔ESP32 BLE path are integrated. The current branch adds the
map-first iPhone planner/navigation UI, authoritative run-state telemetry,
low-MTU BLE fragmentation, and route-only custom BRTE transfer; remaining checks
are physical transfer capture, disconnect/reconnect observation, and GPS
hardware.

Repository state at handoff:

- Branch: `codex/end-to-end-integration`
- Working tree: integration changes pending commit
- ESP firmware: BikeOS 0.5.0
- Toolchain: ESP-IDF 5.5.6
- Host build: CMake + C++20 + SDL2 + zlib
- Physical board: ESP32-S3 rev 0.2, 16 MB flash, 8 MB PSRAM
- Physical LCD: ERC12864SBSF-14 / ST7565R / 128×64, not wired

Recent logical commits:

- `8328054` — shared navigation core, real OSM assets, tests, SDL simulator
- `f242b4c` — ESP navigation runtime, UART GPS source, ST7565R transport
- `ac49b35` — project, testing, GPS, map, and LCD documentation

## What is genuinely working

- Real bounded OpenStreetMap extraction and deterministic `BMAP`/`BRTE` assets.
- CRC-checked map and route decoding on both macOS and ESP32.
- NMEA RMC/GGA parser, GPS filter, deterministic GPS simulator, and configurable
  ESP-IDF UART GPS source.
- Route matching, cumulative progress, next-maneuver distance, arrival,
  persistent off-route detection, reroute throttling, and GPS-loss recovery.
- Exact 128×64 ST7565 page-major framebuffer shared by firmware and simulator.
- Native SDL simulator with live controls and deterministic PNG export.
- Exact 128×64 logical SDL surface with centered nearest-neighbor scaling,
  resize-safe letterboxing, native/4×/8× snapshots, road filtering, named
  maneuver banners, route halo/emphasis, large rider marker, and automatic
  zoom hysteresis/smoothing.
- Physical ESP32 flash and serial verification. The board completed the 1.41 km
  simulated route, entered `REROUTING` after a 50 m deviation, recovered from
  `GPS_LOST`, reached `ARRIVED`, and kept stable free heap.
- Host tests pass, including decoding and rendering the checked-in Amsterdam
  dataset. The previous ESP-IDF image flashed and was serial-verified.
- The new 0.5.0 firmware builds successfully, but the latest flash attempt
  received no serial data from `/dev/cu.usbmodem1101`; repeat after a BOOT/RESET
  sequence or USB reconnect before claiming the new image is on the board.

## What is not complete

- The LCD has not been electrically connected or observed displaying pixels.
- UART GPS is implemented but has not been tested with a physical receiver.
- BLE route/map transfer is implemented with begin/chunk/commit ACK pacing and
  route-ID activation verification; repeat on the physical setup for captured
  serial evidence.
- The iPhone app has a route planner: MapKit destination/start search,
  GraphHopper bicycle routing, route preview/maneuvers, BRTE encoding, and
  route-only transfer. GraphHopper still requires a local API key.
- No outdoor road test has happened.

Do not describe the LCD as physically integrated; its panel is still unwired.

## Display design notes

The current renderer draws geographically correct data, but accuracy alone
does not produce a usable navigation display. At this resolution, the map must
be aggressively selective.

The current renderer implements the following acceptance-oriented composition:

1. Reserve top instruction and bottom maneuver/status bands and clip all map
   geometry to the middle map region.
2. Hide service/footway roads and unnamed minor cycleways; keep the active route
   on a cleared halo with a stronger ahead segment and intersection marker.
3. Use a large rider arrow, 5×7 labels, automatic zoom bands with hysteresis and
   smoothing, and a dedicated turn-now two-line street view.
4. Test at actual 128×64 size plus 4×/8× nearest-neighbor exports.

Acceptance criteria:

- At actual pixel size, the route and rider can be identified within one second.
- The next-turn direction and distance are readable without inspecting the map.
- `REROUTING`, `GPS LOST`, and `ARRIVED` cannot be mistaken for normal guidance.
- Minor roads never overpower the active route.
- Deterministic snapshots cover all four states and are visually reviewed.

## Build and verify

```sh
cd /Users/simon/dev/wrist/BikeOS
cmake -S . -B build-host -DCMAKE_PREFIX_PATH="$(brew --prefix sdl2)"
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
./build-host/bikeos_simulator
```

Export screenshots with:

```sh
./build-host/bikeos_simulator --export-snapshots build-host/snapshots
```

Firmware workflow:

```sh
source /Users/simon/dev/wrist/esp-idf-v5.5.6/export.sh
cd /Users/simon/dev/wrist/BikeOS/firmware
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

The serial device name can change; rediscover it before flashing.

## Physical LCD blocker

The bare 30-pin panel cannot be connected directly with ordinary jumper wires.
It needs a 0.5 mm bottom-contact FPC carrier and the manufacturer charge-pump
capacitor network. ESP GPIO assignments also remain deliberately unset until
the exact ESP carrier silkscreen and LCD adapter are visible. Follow
`docs/hardware-setup.md`; do not guess or power unknown pins.

## Key code locations

- `core/src/map_renderer.cpp` — screen composition and geographic rendering
- `core/src/navigation.cpp` — route matching and navigation states
- `core/src/gps_simulator.cpp` — deterministic ride/events
- `host/simulator/main.cpp` — interactive fake screen and snapshot export
- `firmware/main/navigation_runtime.cpp` — integrated ESP loop
- `firmware/main/st7565_display.cpp` — physical LCD transport
- `tools/prepare_amsterdam_demo.py` — OSM extraction and asset generation

Detailed design and verification notes are linked from the root `README.md`.
