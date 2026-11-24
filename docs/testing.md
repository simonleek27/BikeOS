# Testing

## Host tests

```sh
cmake -S . -B build-host -DCMAKE_PREFIX_PATH="$(brew --prefix sdl2)"
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
```

The suite covers coordinate math, GPS validity/staleness/jump rejection, NMEA
checksums and malformed input, map/route CRC validation, route matching,
maneuver distance, persistent off-route detection and reroute throttling,
framebuffer bounds, rendering, a complete simulated ride, and decoding/rendering
the checked-in Amsterdam assets.

## Deterministic framebuffer snapshots

```sh
./build-host/bikeos_simulator --export-snapshots build-host/snapshots
```

This creates `navigating.png`, `rerouting.png`, `gps-lost.png`, and
`arrived.png` from the same 1,024 framebuffer bytes used by the firmware.

## Screen test bench

`host/virtual_screen/` is a browser test bench for the color TFT. It runs the
real `ColorMapRenderer`, `NavigationEngine` and `GpsFilter` in
`bikeos_virtual_screen`, with the firmware's pacing: navigation steps once a
second, display frames at about 7 fps with the rider gliding between fixes.
The screen shows the exact RGB565 buffer the TFT driver sends to the panel.

```sh
cmake --build build-host --target bikeos_virtual_screen
python3 host/virtual_screen/server.py   # http://localhost:8765
```

Around the screen: a route-progress slider with jumps to each turn, an
auto-follow toggle and speed, and switches for GPS signal, phone connection,
forced off-route, GPS jitter, theme and GPS clock. The iPhone-style panel plans
a route on the OpenStreetMap street graph (fetched once from Overpass and
cached in `host/virtual_screen/.cache/`), sends it to the virtual ESP32
route-only like the app (or with the map, to compare), and has the app's
Start, Pause, End ride, Restart and Trigger off-route controls.

## Physical ESP32 acceptance

The navigation build was flashed to the verified ESP32-S3 on 16 November 2026.
Serial acceptance included asset decode, per-second fixes and frame CRC changes,
a 50 m deviation leading to `REROUTING`, a four-update `GPS_LOST` interval,
recovery, `ARRIVED`, and stable free heap. Physical LCD acceptance remains
pending its carrier/charge-pump circuit.
