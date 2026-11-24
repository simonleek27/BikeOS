# BikeOS handoff

Updated 24 November 2026. The active readiness branch is
`codex/test-readiness`; it is based on `origin/main` at `42594ad` and preserves
the integration work from `codex/end-to-end-integration`.

## Current state

- ESP32-S3 rev 0.2: 16 MB flash, 8 MB PSRAM, native USB Serial/JTAG.
- Firmware 0.5.0 builds with ESP-IDF v5.5.6 and was flashed to
  `/dev/cu.usbmodem101` on 24 November 2026.
- The physical target is the 2.8-inch ILI9341 320×240 color TFT using CS 10,
  RESET 4, DC 5, MOSI 11, and SCK 12. The serial log confirmed panel init and
  about 4.9 fps; direct visual confirmation is still Simon's step.
- Indoor firmware uses deterministic simulated GPS. UART GPS is implemented for
  RX 18/TX 17 at 9600 baud but has not had a physical receiver test.
- BLE route transfer, low-MTU fragmentation, ACK pacing, route activation
  verification, and authoritative run-state telemetry are implemented.
- The iPhone app builds and its 8 protocol tests pass. GraphHopper is configured
  locally through ignored `ios/GraphHopper.xcconfig`; the live bike endpoint
  responded successfully during readiness testing.
- The virtual color-screen bench and the host C++ suite pass.

## What still needs physical observation

1. Confirm the flashed TFT visibly shows the map/status frame.
2. Run iPhone discovery → Ready → demo transfer and save the ESP32 ACK/route-ID
   serial trace.
3. Exercise Start, Pause, Resume, Stop, Restart, then disconnect/reconnect and
   confirm TFT BT green/amber behavior independently of GPS.
4. Connect the NEO-6M, select UART mode, acquire a fix outdoors, and perform a
   short secured bicycle route.

Do not mark any of those as passed from a build log alone. The complete matrix
and exact procedure are in [`docs/test-plan.md`](test-plan.md).

## Build commands

```sh
cd /Users/simon/dev/wrist/BikeOS
cmake -S . -B build-host -DCMAKE_PREFIX_PATH="$(brew --prefix sdl2)"
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
host/virtual_screen/bench.sh

source /Users/simon/dev/wrist/esp-idf-v5.5.6/export.sh
cd firmware
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Open `ios/BikeOS.xcodeproj` in Xcode 26 for the phone app. Keep the GraphHopper
key only in the ignored local xcconfig; do not put it in Git, screenshots, or
serial logs.

## Important code locations

- `core/src/navigation.cpp` — route matching, run state, off-route, arrival
- `core/src/color_renderer.cpp` — production ILI9341 screen composition
- `firmware/main/navigation_runtime.cpp` — GPS/display/navigation runtime
- `firmware/main/ili9341_display.cpp` — physical TFT transport
- `firmware/main/ble_peripheral.cpp` — BLE service and frame reassembly
- `ios/BikeOS/Services/BikeOSBluetooth.swift` — CoreBluetooth transport
- `ios/BikeOS/Services/BikeOSProtocol.swift` — Swift frame/BRTE codecs
- `host/virtual_screen/` — production color renderer test bench
