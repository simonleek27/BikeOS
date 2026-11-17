# Hardware setup

## Verified ESP32 prototype

The prototype was queried through the Espressif ROM bootloader and exercised
with ESP-IDF firmware on 16 November 2026.

| Property | Verified value |
|---|---|
| SoC | ESP32-S3 QFN56 revision 0.2 |
| Flash | 16 MB, DIO at 80 MHz in the current image |
| PSRAM | 8 MB octal PSRAM, memory test passed |
| USB | Native USB Serial/JTAG |
| Bring-up port | `/dev/cu.usbmodem1101` (macOS names can change) |

The exact carrier-board SKU and which header pins are safely free still require
a readable front/back silkscreen photo. That is why no ESP GPIO numbers are
approved for the screen or GPS yet.

## Verified LCD

The screen is an EastRising/BuyDisplay `ERC12864SBSF-14` (`ERC12864-14`
family): 2.7 inch, 128 × 64, ST7565R, blue negative/white LED, 30-contact
0.5 mm-pitch bottom-contact FPC. The controller supports 4-wire serial input,
which is the selected BikeOS interface.

Manufacturer sources:

- [ERC12864-14 product page](https://www.buydisplay.com/2-7-inch-blue-spi-cog-128x64-st7565-lcd-display-emc-and-esd-protection)
- [ERC12864-14 datasheet](https://www.buydisplay.com/download/manual/ERC12864-14_Datasheet.pdf)

### Panel pin table

This is the FPC numbering from the manufacturer datasheet, not an ESP header
pin map.

| FPC | Signal | 4-wire serial use |
|---:|---|---|
| 1 | IRS | High selects internal V0 regulator resistors |
| 2 | P/S | Low selects serial input |
| 3 | C86 | Parallel-interface selector; irrelevant in serial mode |
| 4 | VR | External contrast input; unused when IRS is high |
| 5–9 | V0–V4 | LCD bias capacitor network |
| 10 | CAP2− | Charge-pump capacitor network |
| 11 | CAP2+ | Charge-pump capacitor network |
| 12 | CAP1+ | Charge-pump capacitor network |
| 13 | CAP1− | Charge-pump capacitor network |
| 14 | CAP3+ | Charge-pump capacitor network |
| 15 | VOUT | Charge-pump output capacitor network |
| 16 | VSS | Logic/panel ground |
| 17 | VDD | 2.7–3.3 V logic supply; 3.6 V absolute maximum |
| 18 | D7 / SI | ESP SPI MOSI |
| 19 | D6 / SCL | ESP SPI clock |
| 20–25 | D5–D0 | High impedance in serial mode |
| 26 | /RD | Fixed high in serial mode |
| 27 | /WR | Fixed high in serial mode |
| 28 | A0 | ESP data/command GPIO |
| 29 | /RES | ESP reset GPIO, active low |
| 30 | CS1 | ESP chip-select GPIO, active low |

### Do not plug the bare FPC into ESP jumper wires

This panel is not a five-wire breakout. Pins 5–15 require the datasheet's
external 1 µF charge-pump/bias capacitor network, the 30-pin flex requires a
bottom-contact ZIF connector, and the white backlight is a separate load. The
backlight is specified around 3.0 V and 45 mA typical, so it must not be powered
from an ESP GPIO.

Required before physical LCD testing:

1. A 30-pin, 0.5 mm-pitch **bottom-contact** FFC/FPC breakout or a small carrier
   PCB made for the ERC12864-14.
2. The capacitor network copied exactly from the manufacturer application
   circuit, plus local VDD decoupling.
3. A current-limited backlight path, preferably a transistor/MOSFET switch if
   software control is wanted.
4. Clear ESP carrier-board silkscreen photos so five free GPIOs can be selected
   without colliding with native USB, flash, PSRAM, or boot straps.

With power disconnected, the eventual logic wiring is:

| Carrier net | Destination |
|---|---|
| 3V3 | LCD VDD and the carrier's required high-level straps |
| GND | LCD VSS and ESP GND |
| LCD_SI | FPC 18 (D7/SI) |
| LCD_SCLK | FPC 19 (D6/SCL) |
| LCD_A0 | FPC 28 |
| LCD_RESET_N | FPC 29 |
| LCD_CS_N | FPC 30 |

Do not connect or power the panel until the adapter/capacitor network and ESP
carrier identity are visible. Once they are, this table can be completed with
actual ESP GPIO numbers and checked continuity-by-continuity.
