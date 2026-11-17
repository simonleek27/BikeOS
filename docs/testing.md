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

## Deterministic framebuffer snapshots

```sh
./build-host/bikeos_simulator --export-snapshots build-host/snapshots
```

This creates `navigating.png`, `rerouting.png`, `gps-lost.png`, and
`arrived.png` from the same 1,024 framebuffer bytes used by the firmware.

## Physical ESP32 acceptance

The navigation build was flashed to the verified ESP32-S3 on 16 November 2026.
Serial acceptance included asset decode, per-second fixes and frame CRC changes,
a 50 m deviation leading to `REROUTING`, a four-update `GPS_LOST` interval,
recovery, `ARRIVED`, and stable free heap. Physical LCD acceptance remains
pending its carrier/charge-pump circuit.
