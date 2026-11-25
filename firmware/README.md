# BikeOS firmware

BikeOS firmware is an ESP-IDF C++ application. The current hardware target is
the physically verified ESP32-S3 prototype with 16 MB flash, 8 MB PSRAM, and a
native USB Serial/JTAG connection.

## Toolchain

The project is pinned to ESP-IDF v5.5.6. On Simon's Mac, the checked-out IDF is
located at `/Users/simon/dev/wrist/esp-idf-v5.5.6`.

Activate it in each new terminal:

```sh
source /Users/simon/dev/wrist/esp-idf-v5.5.6/export.sh
```

## Build

```sh
cd /Users/simon/dev/wrist/BikeOS/firmware
idf.py set-target esp32s3
idf.py build
```

## Flash and monitor

The native USB port can change after entering the ROM bootloader. Find it with:

```sh
ls /dev/cu.usbmodem*
```

Then flash and monitor, replacing the port if necessary:

```sh
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

Exit the monitor with `Ctrl-]`.

If auto-reset does not enter download mode: hold **BOOT**, tap and release
**RST/EN**, release **BOOT**, then rerun the flash command.

Expected output includes BikeOS 0.5.0, ESP32-S3 diagnostics, map/route counts,
navigation state, progress, cross-track and maneuver distances, framebuffer
CRCs, reroute requests, and a heartbeat.

## City map partition

The firmware draws the map from the tiled Amsterdam map (`data/amsterdam.btil`)
stored in its own 12 MB `map` flash partition at `0x400000`
(`partitions.csv`). The phone only ever sends routes. If the partition is blank
or fails its index CRC, the firmware logs it and falls back to the small demo
map embedded in the app.

`idf.py flash` writes the bootloader, partition table and app but never the
map, so app updates stay fast. Flash the map once, and again whenever
`data/amsterdam.btil` is rebuilt:

```sh
idf.py -p /dev/cu.usbmodem1101 map-flash
```

or with esptool directly (same result, no build needed):

```sh
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem1101 -b 921600 write_flash 0x400000 ../data/amsterdam.btil
```

Switching an existing checkout to this partition table: the generated
`sdkconfig` keeps the old single-app table until it is regenerated, so run
`rm sdkconfig && idf.py build` (or set *Partition Table → Custom partition
table CSV* to `partitions.csv` in menuconfig), then `idf.py flash` and
`idf.py map-flash`. Boot logs `City map: N tiles ...` when the partition is
in use and `Map tiles around c,r: decoded ...` each time the rider crosses
into another tile.

## ILI9341 TFT and diagnostic mode

The checked-in `sdkconfig` selects the 2.8-inch ILI9341 navigation display. It
initializes the panel at 320x240 over SPI and renders the production BikeOS map
frame. A separate color-bar diagnostic can be enabled temporarily when checking
power or SPI wiring; it is not the normal navigation image.

The configured signal wiring is:

| TFT pin | ESP32-S3 GPIO |
| --- | ---: |
| CS | 10 |
| RESET | 4 |
| DC/RS | 5 |
| SDI/MOSI | 11 |
| SCK | 12 |
| GND | GND |
| VCC | 5Vin **after** the IN-OUT bridge is installed |
| LED | 3V3 |

Do not solder or change the IN-OUT selector while USB power is connected. With
the board unplugged, bridge only the two marked IN-OUT pads, inspect the joint,
then connect the TFT. Keep TFT J1 open for 5V operation and leave MISO, touch,
and SD pins disconnected. If the board or display gets hot or repeatedly
resets, unplug it immediately.

Build and flash after the power bridge and wiring have been checked:

```sh
source /Users/simon/dev/wrist/esp-idf-v5.5.6/export.sh
cd /Users/simon/dev/wrist/BikeOS/firmware
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

After a diagnostic check, turn off `CONFIG_BIKEOS_DISPLAY_TEST_PATTERN` in
`idf.py menuconfig` before testing navigation. The checked-in default already
has the pattern disabled.

## Indoor demo mode and navigation controls

The checked-in firmware defaults to a deterministic indoor GPS simulator, so
the complete route can be tested without a GPS antenna. After the route is
transferred from the iPhone, the simulator waits for **Start** and then advances
at 4.5 m/s. It emits repeatable position, heading, maneuver, off-route, and GPS
loss states that the color TFT and iPhone display. **Restart** returns to the
first route point.

The iPhone controls map to BLE control frames: **Start**, **Pause**, **Resume**,
**Restart**, **Stop**, and **Test off-route**. The app does not claim a route is
active until it receives the ESP32 commit acknowledgement and a status frame
with the matching route ID.

For a physical demo:

1. Flash this firmware and leave the ESP32 powered by USB.
2. Open the BikeOS app on a real iPhone, tap **Connect**, then **Send demo route**.
3. Tap **Start** and watch the rider marker on both the iPhone and ILI9341.
4. Use **Pause/Resume** to stop and continue the simulator, **Restart** to reset,
   and **Test off-route** to exercise rerouting behavior.

The firmware's `sdkconfig.defaults` selects the verified ILI9341 wiring and
disables the test pattern. Use `idf.py menuconfig` only when changing to a
different verified display or GPS source; do not guess GPIOs.

## Hardware source selection

Run `idf.py menuconfig`, then open **BikeOS hardware**. The safe defaults are the
deterministic GPS simulator and virtual 128 × 64 framebuffer. Real UART GPS and
physical ST7565R choices expose their pin settings; unset pins default to `-1`
and cause startup to fail instead of touching unknown hardware.

The ILI9341 navigation target is enabled for the photo-verified GPIO map. The
ST7565R option remains available only as a historical alternate target and
should stay disabled unless that separate display is wired and verified.

## Standalone real-GPS test

The route can be loaded before going outside. After wiring the verified GPS
module, choose `BikeOS hardware -> GPS source -> UART NMEA` in `idf.py menuconfig`
and flash that build once. In UART mode a transferred route is armed in
`NOT_STARTED`; press **Start** in the phone app, then take the ESP32/GPS near a
window or outdoors. The firmware reports `WAITING_GPS` until a valid NMEA fix,
then starts advancing from that fix. No Mac connection is needed after flashing:
power the ESP32 from a USB power bank and connect the phone over BLE.

For an Amsterdam route, the GPS must actually be near that route for map
matching. A valid fix somewhere else is still visible as GPS telemetry, but it
cannot advance an Amsterdam route and may be reported off-route/error. For the
first field test, create a route near the GPS location or use the simulator
build for an indoor end-to-end test.

