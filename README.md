# BikeOS

BikeOS is a phone-assisted bicycle navigator for an ESP32-S3 and a 128 × 64
monochrome ST7565R LCD. The phone prepares routes; the ESP32 tracks the ride
from GPS, renders local OpenStreetMap geometry, detects deviations, and requests
a reroute when needed.

## Current status

The `codex/navigation-map` branch contains a shared C++ navigation core used by
both the ESP-IDF firmware and the native desktop simulator. It currently has:

- a checked and versioned compact map/route format;
- a real bounded OpenStreetMap extract and bicycle route through Amsterdam;
- NMEA RMC/GGA parsing, GPS filtering, deterministic GPS simulation, and a
  configurable ESP-IDF UART source;
- route matching, maneuver distance, arrival, GPS-loss, off-route, and reroute
  state handling;
- a 1,024-byte ST7565 page framebuffer and moving-map renderer;
- a native SDL fake-screen demo using the exact same C++ renderer and assets as
  the firmware;
- an ESP-IDF ST7565R SPI transport with GPIOs deliberately unset until the
  carrier wiring is built and checked.

The real ESP32-S3 prototype has been built, flashed, and observed through a full
1.41 km simulated ride. It loaded 1,017 OSM road polylines, rerouted after a
50 m deviation, recovered from GPS loss, reached `ARRIVED`, and retained stable
heap usage.

## Run the fake screen

Dependencies on macOS: CMake, SDL2, and zlib.

```sh
cmake -S . -B build-host -DCMAKE_PREFIX_PATH="$(brew --prefix sdl2)"
cmake --build build-host -j
./build-host/bikeos_simulator
```

Controls: `Space` pause, `R` reset, `+/-` speed, `[`/`]` zoom, `O` force an
off-route event, `G` force GPS loss, `V` toggle the route, `S` save a PNG, and
`Esc` quit.

To export deterministic PNGs without opening a window:

```sh
./build-host/bikeos_simulator --export-snapshots build-host/snapshots
```

## Build and test

```sh
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
```

See [firmware/README.md](firmware/README.md) for ESP-IDF commands and
[docs/hardware-setup.md](docs/hardware-setup.md) before connecting the LCD.

OpenStreetMap-derived data is © OpenStreetMap contributors and licensed under
ODbL 1.0. See [OpenStreetMap copyright](https://www.openstreetmap.org/copyright).
