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

Expected output includes BikeOS 0.2.0, ESP32-S3 diagnostics, map/route counts,
navigation state, progress, cross-track and maneuver distances, framebuffer
CRCs, reroute requests, and a heartbeat.

## Hardware source selection

Run `idf.py menuconfig`, then open **BikeOS hardware**. The safe defaults are the
deterministic GPS simulator and virtual 128 × 64 framebuffer. Real UART GPS and
physical ST7565R choices expose their pin settings; unset pins default to `-1`
and cause startup to fail instead of touching unknown hardware.

Do not enable the physical display until the carrier/capacitor circuit and ESP
GPIO map in `docs/hardware-setup.md` are complete.

