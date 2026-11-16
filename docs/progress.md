# Development progress

Updated: 16 November 2026

| Feature | Implementation | Build/test | Physical test | Blocker / next action |
|---|---|---|---|---|
| ESP32 identification | Complete | ESP ROM query passed | Complete | Carrier-board SKU still needs readable silkscreen photo |
| ESP-IDF toolchain | Complete | ESP-IDF v5.5.6 / GCC 14.2.0 passed | N/A | Keep v5.5.6 pinned for reproducible builds |
| Startup diagnostics | Complete | Build passed; 191 kB image | Boot and 20 stable heartbeats verified | Milestone 1 complete |
| LCD identification | Not started | N/A | Not wired | Need front/back/label photos before assigning GPIOs |
| LCD driver/test | Not started | Not run | Not run | Confirm exact display model and interface first |
| GPS abstraction | Planned | Not run | Hardware not connected | Implement simulator after bring-up |
| Navigation engine | Planned | Not run | N/A | Starts after ESP32 bring-up |
| BLE protocol/firmware | Planned | Not run | Not run | Starts after navigation/protocol foundation |
| iPhone app | Planned | Not run | Not run | Starts after protocol is stable |

## Verified prototype facts

- Chip: ESP32-S3 QFN56, revision 0.2.
- CPU/radio: dual-core 240 MHz, Wi-Fi, Bluetooth 5 LE.
- PSRAM: 8 MB embedded PSRAM reported by the ROM.
- Flash: 16 MB external quad flash, manufacturer ID `0x68`, device ID `0x4018`.
- USB: native USB Serial/JTAG.
- Current bootloader port: `/dev/cu.usbmodem1101` (macOS device names can change).
- The original N8R8 assumption is superseded for this prototype by the measured
  16 MB flash / 8 MB PSRAM configuration.

## Immediate sequence

1. Confirm the LCD from physical labels and manufacturer documentation.
2. Publish a verified wiring table, then wire it manually with power removed.
3. Implement and run a text/shapes/graphics display test.
4. Continue with simulated GPS, navigation, protocol, BLE, and the iPhone app.

## First flash result

The initial BikeOS firmware was built and flashed on 16 November 2026. The
bootloader, partition table, and 190,960-byte application image all passed
write-time SHA verification. After a normal reset with BOOT released, it started
from flash (`SPI_FAST_FLASH_BOOT`), passed the 8 MB PSRAM memory test, reported
the expected 16 MB flash and 8 MB PSRAM, initialized BikeOS 0.1.0, and emitted
20 consecutive one-second heartbeats. Free heap remained constant at 8,766,131
bytes throughout the observation. Milestone 1 is complete.

