# GPS sources

`GpsFix` is the source-independent contract for simulated, UART NMEA, and future
iPhone fixes. The filter rejects invalid coordinates, stale fixes, and movement
that exceeds the configured speed plus jump allowance.

The NMEA parser validates checksums and supports `RMC` and `GGA` sentences. It
extracts position, fix validity/quality, speed, heading, and an accuracy estimate
without retaining unbounded input.

The ESP-IDF UART source uses a non-blocking read, a 128-byte line limit, and the
same parser. It is compiled now but remains disabled until the physical GPS
module and an ESP RX pin are verified. Configure it with `idf.py menuconfig` in
`BikeOS hardware -> GPS source`; selecting UART while the RX GPIO is still `-1`
fails safely at startup.

Typical NMEA wiring after a module is verified is module `TX -> ESP RX`, common
ground, and the module's documented supply voltage. Do not assume that every
GPS breakout's `VCC` is 3.3 V or that its UART output is level-shifted.

## NEO-6M (GY-NEO6MV2) wiring

The module in hand is a GY-NEO6MV2: u-blox NEO-6M with an onboard 3.3 V
regulator, a 4-pin header (`VCC RX TX GND`), and a ceramic patch antenna. It
outputs NMEA at 9600 baud, 1 Hz, out of the box, which is what the firmware
expects. Its UART is 3.3 V logic, so no level shifter is needed.

| GPS pin | ESP32 header label | Notes |
|---|---|---|
| VCC | `3V3` | Module draws about 45 mA; fine from the ESP 3V3 rail |
| GND | `GND` | Common ground with the ESP and TFT |
| TX | `18` | GPS TX -> ESP RX (UART1) |
| RX | `17` | ESP TX -> GPS RX; only needed to send config later |

GPIO17/18 are clear of the TFT (4, 5, 10, 11, 12), native USB (19, 20),
octal PSRAM (35-37), the COM bridge (43, 44), and the strapping pins (0, 3,
45, 46). `firmware/sdkconfig.defaults` selects this configuration. A local
`firmware/sdkconfig` overrides the defaults, so delete it or set
`BikeOS hardware -> GPS source` in `idf.py menuconfig` after pulling.

First fix: put the antenna (the beige ceramic square) face up with a clear
view of the sky, ideally outdoors or at a window. A cold start takes 30 s to
several minutes. The small LED on the board starts blinking once per second
when it has a fix. The firmware log line `Listening for NMEA on UART1 RX
GPIO18 at 9600 baud` confirms the UART came up.

The NEO-6M does not support UBX NAV-PVT or 5 Hz with the same margin as the
NEO-M8N assumed in `architecture.md`. Stay on NMEA at 1 Hz for now.
