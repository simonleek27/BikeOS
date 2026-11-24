# BikeOS end-to-end test plan

Updated 24 November 2026 on branch `codex/test-readiness`.

This is the working acceptance matrix. **Pass** means the test was actually
executed in this environment; **Not executed** means it still requires Simon's
physical phone, GPS, or bicycle. A physical display initialization log is not
treated as proof that the panel is visibly rendering.

## Stage A — software verification

| ID | Subsystem | Preconditions | Procedure | Expected result | Actual result | Status / evidence |
|---|---|---|---|---|---|---|
| A-01 | Host build | CMake and SDL2 installed | Configure and build `build-host` | All targets compile with warnings as errors | Core, simulator, color preview, and virtual-screen engine built | **Pass** — `cmake --build build-host -j2` |
| A-02 | Core tests | A-01 | Run CTest | All registered host tests pass | 1/1 CTest target passed | **Pass** — `ctest --test-dir build-host --output-on-failure` |
| A-03 | Frame snapshots | A-01 | Export deterministic snapshots | Navigation, turns, rerouting, GPS loss, and arrival render without a crash | Snapshot PNGs exported for normal, turn, rerouting, GPS-lost, and arrived states | **Pass** — `build-host/readiness-snapshots-20261124/` |
| A-04 | Color renderer | A-01 | Inspect virtual-screen RGB565 frame | Map, route, rider, instruction card, GPS chip, and BT chip are visible | 320×240 frame inspected; map and status chips are readable | **Pass** — `bikeos_virtual_screen` frame, 153,600 bytes |
| A-05 | Virtual run state | A-01 | Exercise start → pause → resume → stop → restart | Paused progress/speed stay fixed; resume continues; restart resets | Progress held at 2.2 m while paused; resume advanced; restart returned to 0 m | **Pass** — virtual bench API smoke test |
| A-06 | Virtual fault states | A-01 | Toggle phone, GPS, and off-route controls | Status changes to phone disconnected/GPS lost/rerouting and recovers | Phone disconnect/reconnect and GPS-lost transitions verified | **Pass** — virtual bench API smoke test |
| A-07 | BLE protocol | Swift/C++ protocol sources | Run host protocol tests and iOS unit tests | CRC, fragmentation, ACK, status, route limits, and malformed data are rejected safely | Existing C++ suite passed; iOS protocol suite passed 8 tests | **Pass** — CTest + `xcodebuild test` |
| A-08 | GraphHopper | Local ignored `ios/GraphHopper.xcconfig` | Request a bicycle route for two Amsterdam points | Provider returns geometry and instructions | 1 path, 1,009.2 m, 29 points, 12 instructions | **Pass** — live endpoint check; key never printed or committed |
| A-09 | iOS build | Xcode 26.3 | Build simulator target without signing | App bundle is valid and contains an executable | Build succeeded | **Pass** — `xcodebuild ... -sdk iphonesimulator build CODE_SIGNING_ALLOWED=NO` |
| A-10 | iOS unit tests | iPhone 17 simulator runtime | Run BikeOS test scheme | All tests pass | 8 passed, 0 failed, 0 skipped | **Pass** — xcresult 24 Nov 2026 |

### Stage A commands

```sh
cmake -S . -B build-host -DCMAKE_PREFIX_PATH="$(brew --prefix sdl2)"
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
./build-host/bikeos_simulator --export-snapshots build-host/snapshots

xcodebuild -project ios/BikeOS.xcodeproj -scheme BikeOS \
  -destination 'id=ECD35C85-7B7A-4134-BC62-351D3FD43F2D' \
  CODE_SIGNING_ALLOWED=NO test
```

## Stage B — physical bench verification

| ID | Subsystem | Preconditions | Procedure | Expected result | Actual result | Status / evidence |
|---|---|---|---|---|---|---|
| B-01 | Board identity | ESP32 connected | Flash/query the board | ESP32-S3, 16 MB flash, 8 MB PSRAM are reported | Chip revision v0.2, 16 MB, 8 MB PSRAM reported | **Pass** — esptool and serial monitor |
| B-02 | Firmware build | ESP-IDF v5.5.6 | Run `idf.py build` | Firmware links and fits partition | `bikeos.bin` 0xB0030; 31% app partition free | **Pass** — firmware build |
| B-03 | Firmware flash | Verified `/dev/cu.usbmodem101` | Flash the built image | Hash verification and hard reset succeed | All three images written and verified | **Pass** — esptool log |
| B-04 | Firmware boot | B-03 | Monitor startup | Version, GPS mode, display, BLE advertising, and heartbeat appear | BikeOS 0.5.0, simulated GPS, ILI9341 init, BLE advertising, stable heap | **Pass** — serial monitor |
| B-05 | TFT visual output | TFT wired and powered safely | Reset board and inspect panel | Map/status frame is visibly rendered | Serial reports ILI9341 initialized and 4.9 fps; panel was not visually observed by the agent during this run | **Not executed** — Simon must confirm the screen image |
| B-06 | BLE discovery | B-04, iPhone nearby | Connect from BikeOS app | App reaches Connected then Ready | Prior physical milestone recorded discovery/connection; current reconnect not observed in this run | **Needs physical retest** |
| B-07 | Demo transfer | B-06 | Send bundled demo route | Begin/chunk/commit ACKs and matching active route ID | Protocol and firmware paths are covered in automated tests; live transfer log still needed | **Needs physical retest** |
| B-08 | Controls | B-07 | Start, pause, resume, stop, restart | TFT and phone agree with acknowledged run state | Virtual equivalent passed; physical paired control cycle not observed in this run | **Needs physical retest** |
| B-09 | Disconnect indicator | B-08 | Disconnect/reconnect phone | TFT BT indicator changes green ↔ amber independently of GPS | Renderer now maps connected BT to green and disconnected BT to amber; physical color transition not observed | **Needs physical retest** |

## Stage C — outdoor GPS verification

| ID | Subsystem | Preconditions | Procedure | Expected result | Actual result | Status |
|---|---|---|---|---|---|---|
| C-01 | UART wiring | NEO-6M and verified carrier wiring | Select UART GPS, flash, monitor | NMEA bytes arrive on RX 18 at 9600 baud | No GPS module was connected | **Not executed** |
| C-02 | Acquisition | Outdoor sky view | Wait for a valid fix | Validity and accuracy become visible without stale data | No physical GPS test | **Not executed** |
| C-03 | Stationary behavior | Valid fix | Leave bike still for 2 minutes | Filter suppresses jitter and heading remains sensible | Core filter tests only | **Not executed** |
| C-04 | Walking movement | Valid fix | Walk a short route | Position and heading advance; loss/recovery is safe | No physical GPS test | **Not executed** |

## Stage D — bicycle navigation verification

| ID | Subsystem | Preconditions | Procedure | Expected result | Actual result | Status |
|---|---|---|---|---|---|---|
| D-01 | Safe mounting | Route transferred and phone locked/mounted | Secure ESP32/TFT/GPS and phone before moving | No loose wiring; controls are not operated while riding | Must be performed by Simon | **Not executed** |
| D-02 | In-coverage route | Short Amsterdam route inside stored BMAP | Ride with multiple turns | TFT route, maneuver, remaining distance, and phone status agree | Virtual route and renderer verified; no bicycle ride | **Not executed** |
| D-03 | Intentional deviation | D-02 | Leave route briefly | Off-route/rerouting is shown without corrupting active route | Virtual off-route path covered; no physical ride | **Not executed** |
| D-04 | Arrival | D-02 | Complete route | Both devices show arrived; restart is available | Core and virtual arrival paths covered; no physical ride | **Not executed** |

## Physical test procedure for Simon

1. Keep the ESP32 powered by USB and verify the TFT/GPS wiring is not under
   mechanical strain. Do not change J1 or the ESP32 IN-OUT selector while
   powered.
2. Open `ios/BikeOS.xcodeproj`, choose the iPhone, and run the app. Tap
   **Connect** and wait until the status says **Ready**.
3. Send the bundled Amsterdam demo route first. Wait for the activated-route
   confirmation before starting navigation.
4. Press **Start**, then **Pause**, wait two seconds, and confirm both riders
   stop. Press **Resume**, then **Stop**, then **Restart**.
5. Turn the iPhone Bluetooth connection off or disconnect from the app. Confirm
   the TFT BT indicator is amber/orange, not green. Reconnect and confirm it is
   green. GPS status must remain independent.
6. For real routing, search an Amsterdam destination, choose the current
   location or a manual start, calculate, preview, and send the route. Keep the
   first route inside the stored Museumplein coverage.
7. Only after the bench test passes, secure the bicycle, lock the phone, and
   ride a short route. Do not debug or tap the phone while moving.

## Current blockers and limitations

- Physical TFT visibility after the latest flash still needs Simon's direct
  observation; serial initialization alone is insufficient evidence.
- Live iPhone↔ESP32 transfer, disconnect/reconnect indicator transitions, and
  all outdoor/bicycle stages require the physical phone and hardware.
- The GraphHopper key is local and ignored by Git. The live endpoint responded
  successfully in this environment; rotate the key if it is ever exposed.
- BRTE v1 has a bounded coordinate window and the stored BMAP covers only the
  Amsterdam demo area. Unsupported geometry is rejected; broader offline maps
  are future work.
