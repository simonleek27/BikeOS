# BikeOS

BikeOS is a phone-assisted bicycle navigator. The iPhone plans or selects a
route, sends a validated BRTE bundle over BLE, and the ESP32-S3 owns the active
ride: GPS matching, maneuver progress, off-route detection, and the display.

## Current hardware and software

| Part | Current configuration |
|---|---|
| MCU | ESP32-S3, revision 0.2, 16 MB flash, 8 MB PSRAM |
| Display | 2.8-inch ILI9341 color TFT, 320×240 RGB565, SPI |
| TFT GPIOs | CS 10, RESET 4, DC 5, MOSI 11, SCK 12 |
| GPS | GY-NEO6MV2/NEO-6M over UART1, ESP RX 18, TX 17, 9600 baud (optional) |
| Phone | SwiftUI iOS app using CoreBluetooth and MapKit |
| Routing | GraphHopper `bike` profile for real routes; bundled Amsterdam demo works offline |
| Shared core | C++ navigation, GPS filtering, BMAP/BRTE codecs, and RGB565 renderer |
| Test bench | `host/virtual_screen/` runs the production color renderer and navigation engine |

The checked-in firmware defaults to deterministic indoor GPS simulation. The
physical TFT is enabled in the checked-in `firmware/sdkconfig`; UART GPS and
display-only diagnostics are selectable through `idf.py menuconfig`.

## Build and run the host suite

```sh
cmake -S . -B build-host -DCMAKE_PREFIX_PATH="$(brew --prefix sdl2)"
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
```

The host suite covers coordinate math, NMEA parsing, GPS freshness and jump
rejection, BMAP/BRTE CRCs, route matching, maneuvers, off-route/reroute state,
framebuffer bounds, color rendering, and a complete simulated ride.

## Run the virtual color-TFT bench

The bench uses the same `NavigationEngine`, `GpsFilter`, and `ColorMapRenderer`
as the firmware and produces the exact 320×240 RGB565 frame sent to the TFT.

Native macOS window (first run creates a private pywebview environment):

```sh
host/virtual_screen/bench.sh
```

Browser fallback:

```sh
python3 host/virtual_screen/server.py
# open http://localhost:8765
```

The bench can plan routes inside the bundled Amsterdam graph, send route-only
bundles, exercise Start/Pause/Resume/Restart/Stop, toggle GPS and phone state,
force off-route/rerouting, switch day/night themes, and export a screenshot.

## Build and flash the ESP32-S3

ESP-IDF v5.5.6 is expected at `/Users/simon/dev/wrist/esp-idf-v5.5.6`:

```sh
source /Users/simon/dev/wrist/esp-idf-v5.5.6/export.sh
cd firmware
idf.py set-target esp32s3
idf.py build
ls /dev/cu.usbmodem*
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

The normal image advertises as `BikeOS`, initializes the ILI9341, and starts in
indoor simulation mode. Use `Ctrl-]` to leave the monitor. Hold BOOT, tap
RST/EN, release BOOT, and retry flashing if the board does not enter download
mode automatically.

For real GPS, open `idf.py menuconfig` → **BikeOS hardware** → choose the UART
source and enter only the verified GPIOs. For a display-only diagnostic, enable
the ILI9341 color-bar test pattern, flash it, then disable the pattern before
testing navigation.

## Run the iPhone app

Open `ios/BikeOS.xcodeproj` in Xcode 26 and select the BikeOS target. The local
GraphHopper key belongs in the ignored file below; never commit it:

```sh
cp ios/GraphHopper.example.xcconfig ios/GraphHopper.xcconfig
# edit the copied file, then build/run BikeOS in Xcode
```

The app can calculate and preview a bicycle route without an ESP32. To send it
and navigate, leave the ESP32 powered, tap **Connect**, wait for **Ready**, send
the route, and wait for the matching activated-route acknowledgement before
pressing **Start**. The ESP32 status stream controls the phone marker and
authoritative run state; Pause/Resume does not use an independent phone timer.

## Route and map limitations

Custom routes are encoded as BRTE v1 with CRCs, maneuvers, street names, and
signed 16-bit coordinate offsets. Geometry outside that representable window is
rejected instead of being truncated. Custom transfers are route-only, so the
ESP32 keeps its stored Amsterdam BMAP and draws the route itself outside that
map area without showing unrelated roads. Broader offline map coverage and
multi-waypoint planning remain future work.

## Hardware safety

Read [docs/hardware-setup.md](docs/hardware-setup.md) and
[docs/tft-wiring.md](docs/tft-wiring.md) before changing power or wiring. Do
not guess a carrier pinout, power a TFT from an ESP GPIO, or change the J1/
IN-OUT selectors while USB power is connected.

## Documentation

- [Physical test plan](docs/test-plan.md)
- [Testing and virtual bench](docs/testing.md)
- [ESP32 firmware](firmware/README.md)
- [BLE protocol](docs/ble-ios.md)
- [Routing and map coverage](docs/routing.md)
- [GPS setup](docs/gps.md)
- [Current progress](docs/progress.md)

OpenStreetMap-derived data is © OpenStreetMap contributors and licensed under
ODbL 1.0. See [OpenStreetMap copyright](https://www.openstreetmap.org/copyright).
