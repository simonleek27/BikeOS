# Hardware setup

This document describes the hardware actually used by the current firmware.
The old ST7565R/LCD plan is historical and does not apply to the color TFT.

## Verified ESP32-S3 prototype

| Property | Verified value |
|---|---|
| SoC | ESP32-S3 QFN56, revision 0.2 |
| Flash | 16 MB, DIO at 80 MHz in the current image |
| PSRAM | 8 MB octal PSRAM; memory test passed |
| USB | Native USB Serial/JTAG |
| Last verified macOS port | `/dev/cu.usbmodem101` (the number can change) |
| ESP-IDF | v5.5.6 |

The carrier is the 44-pin dual-USB ESP32-S3-N16R8-style board shown in the
project wiring photos. GPIO19/20 are native USB, GPIO35–37 are PSRAM-connected,
GPIO43/44 are used by the alternate USB-UART path, and GPIO0/3/45/46 are boot
straps. Do not reuse those pins without checking the carrier documentation.

## Verified display

The physical module is a 2.8-inch, 240×320 ILI9341 SPI color TFT (V1.2-style
14-pin display header with separate SD/touch pins). The exact vendor SKU is not
needed by the driver; the signal names and controller were verified from the
module documentation and the working color-bar test.

### TFT wiring

| TFT pin/signal | ESP32 header | GPIO | Notes |
|---|---|---:|---|
| CS | 10 | 10 | Active-low chip select |
| RESET | 4 | 4 | Active-low panel reset |
| DC/RS | 5 | 5 | Data/command select |
| SDI/MOSI | 11 | 11 | SPI controller-to-panel data |
| SCK | 12 | 12 | SPI clock |
| GND | GND | — | Common ground |
| VCC | `5Vin` with TFT J1 open | — | Only after the ESP32 IN-OUT selector is verified |
| LED | `3V3` power header | — | Always-on backlight; never use an ESP GPIO |
| SDO/MISO | disconnected | — | Not needed for the write-only driver |

The firmware uses SPI2/host 1, 26 MHz, landscape 320×240. Touch and SD pins are
not part of the current BikeOS design.

### Power safety

- With TFT **J1 open**, its VCC input expects 5 V through the module regulator.
- With J1 closed, the module expects 3.3 V. Do not feed 3.3 V into VCC while J1
  is open.
- The ESP32 `5Vin` output is available only when the carrier's `IN-OUT` bridge
  is installed. Do not bridge or solder it while USB power is connected.
- Use only the ESP32 USB cable as the first-test power source. Stop immediately
  if the TFT or ESP32 becomes hot or repeatedly resets.
- The LED backlight is a power load; connect it to the verified 3V3 header, not a
  GPIO.

Before applying power, with USB disconnected, check the TFT J1 state, verify
the VCC-to-GND path is not a hard short, and confirm each header label against
the carrier silkscreen. A lit backlight is not proof that SPI is working.

## Optional NEO-6M GPS

The current indoor image uses deterministic simulated GPS. For a real UART
receiver, the verified assignment is:

| GPS signal | ESP32 | GPIO |
|---|---|---:|
| GPS TX | ESP RX | 18 |
| GPS RX | ESP TX | 17 |
| GPS VCC | 3V3 | — |
| GPS GND | GND | — |

Configure **BikeOS hardware → GPS source → TTL UART NMEA receiver** in
`idf.py menuconfig`, keeping UART1 and 9600 baud unless the module is verified
otherwise. Do not claim a real GPS test passed until the serial log shows NMEA
bytes and a valid fix.

## Build configuration modes

The checked-in `firmware/sdkconfig` is the physical ILI9341 + simulated-GPS
configuration used for indoor testing. `idf.py menuconfig` exposes:

1. **Indoor simulator:** deterministic GPS, ILI9341 navigation frame (default).
2. **Outdoor UART GPS:** UART1 on GPIO18/17 at 9600 baud, same display driver.
3. **Display diagnostic:** ILI9341 color bars; disable the test pattern again
   before evaluating navigation.

The checked-in `sdkconfig` is intentional and overrides defaults. After changing
menuconfig, rebuild and inspect the startup lines for the selected GPS source,
display target, GPIOs, and BLE state.

See [tft-wiring.md](tft-wiring.md), [gps.md](gps.md), and
[test-plan.md](test-plan.md) for the physical checklist.
