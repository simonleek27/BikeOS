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
