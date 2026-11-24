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

## Deterministic snapshots

```sh
./build-host/bikeos_simulator --export-snapshots build-host/snapshots
```

This creates deterministic navigation-state PNGs from the legacy monochrome
simulator. The production physical target is the 320×240 RGB565 color renderer;
the virtual screen bench below is the authoritative color-TFT preview.

## Screen test bench

`host/virtual_screen/` is a browser test bench for the color TFT. It runs the
real `ColorMapRenderer`, `NavigationEngine` and `GpsFilter` in
`bikeos_virtual_screen`, with the firmware's pacing: navigation steps once a
second, display frames at about 7 fps with the rider gliding between fixes.
The screen shows the exact RGB565 buffer the TFT driver sends to the panel.

It opens in its own window (pywebview, WebKit):

```sh
host/virtual_screen/bench.sh
```

or double-click `host/virtual_screen/BikeOS Bench.app`. The first run creates
`host/virtual_screen/.venv` with pywebview; every run rebuilds
`bikeos_virtual_screen` if its C++ sources changed. Because the repo is under
`~/Documents`, macOS asks once whether BikeOS Bench may access the Documents
folder; allow it (System Settings → Privacy & Security → Files and Folders).
Output from the app goes to `~/Library/Logs/BikeOS Bench.log`.

To use a browser instead: `python3 host/virtual_screen/server.py`, then open
http://localhost:8765.

Around the screen: a route-progress slider with jumps to each turn, an
auto-follow toggle and speed, and switches for GPS signal, phone connection,
forced off-route, GPS jitter, theme and GPS clock. The iPhone-style panel plans
a route on the OpenStreetMap street graph (fetched once from Overpass and
cached in `host/virtual_screen/.cache/`), sends it to the virtual ESP32
route-only like the app (or with the map, to compare), and has the app's
Start, Pause, End ride, Restart and Trigger off-route controls.

## Physical ESP32 acceptance

The verified ESP32-S3 was reflashed on 24 November 2026 through
`/dev/cu.usbmodem101` with ESP-IDF v5.5.6. The serial log confirmed ESP32-S3
revision 0.2, 16 MB flash, 8 MB PSRAM, deterministic GPS mode, ILI9341
initialization at 26 MHz, BLE advertising as BikeOS, and stable heartbeats with
about 8.2 MB free heap. The firmware reported about 4.9 display frames per
second (render 79 ms, flush 8 ms).

That log proves the driver initialized; it does not prove the TFT was visibly
readable from the agent's environment. Simon must complete the visual TFT,
iPhone BLE transfer, disconnect/reconnect, and physical ride checks in
[`docs/test-plan.md`](test-plan.md).
