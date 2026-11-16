# BikeOS handoff

## Read this first

The navigation stack works, but the current 128×64 interface does not. The
latest user review was that the fake screen is effectively impossible to read.
Treat display readability as the first task—not as polish to defer until after
BLE or the iPhone app.

Repository state at handoff:

- Branch: `codex/navigation-map`
- Working tree: clean when this handoff was written
- ESP firmware: BikeOS 0.2.0
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
- Physical ESP32 flash and serial verification. The board completed the 1.41 km
  simulated route, entered `REROUTING` after a 50 m deviation, recovered from
  `GPS_LOST`, reached `ARRIVED`, and kept stable free heap.
- Host tests pass, including decoding and rendering the checked-in Amsterdam
  dataset. ESP-IDF firmware builds and flashes successfully.

## What is not complete

- The screen layout is not readable enough for riding.
- The LCD has not been electrically connected or observed displaying pixels.
- UART GPS is implemented but has not been tested with a physical receiver.
- BLE route/map transfer is not implemented.
- The iPhone app is not implemented.
- No outdoor road test has happened.

Do not describe the simulator as a completed UI or the LCD as physically
integrated.

## First task: make 128×64 readable

The current renderer draws geographically correct data, but accuracy alone
does not produce a usable navigation display. At this resolution, the map must
be aggressively selective.

Recommended changes, in order:

1. Reserve a fixed 12–16 pixel status band and never let map geometry enter it.
2. Show only route-adjacent streets. Hide footways/service roads at normal zoom
   and introduce road-class/zoom visibility thresholds.
3. Increase route separation: clear a narrow halo underneath it, then draw a
   strong 3-pixel route line.
4. Keep a large, unmistakable rider marker near the center. A filled 7×9 arrow
   with a cleared halo is a reasonable starting point.
5. Replace the 3×5 status font with a deliberately designed 5×7 subset for
   digits, distance units, GPS/BLE state, and maneuver labels.
6. Prefer maneuver information over street density. When a turn is close,
   switch to a large arrow + distance view if the map cannot remain legible.
7. Test at **actual 128×64 size**, not only the 8× SDL window.

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
