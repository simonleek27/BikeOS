# BikeOS 2.8-inch TFT wiring handoff

**Status: J1 reported OPEN — VCC=5V, LED=3V3. Perform the unpowered checks
below before applying USB power.**

This document is hardware documentation only. It does not change firmware,
GPIO configuration, the navigation engine, the iPhone app, or the BLE protocol.

## 1. What is verified

### ESP32

The silicon and firmware target are verified:

| Property | Verified value |
|---|---|
| SoC | ESP32-S3 QFN56, revision 0.2 |
| Flash | 16 MB |
| PSRAM | 8 MB octal |
| USB | Native USB Serial/JTAG |
| ESP-IDF | 5.5.6 |
| Current USB port | `/dev/cu.usbmodem1101` (can change) |

The supplied photo reference shows the **44-pin, dual-USB ESP32-S3-N16R8
carrier layout**: two USB-C sockets labelled `COM` and `USB`, `RST`/`BOOT`
buttons, and headers labelled with GPIO numbers, `3V3`, `5V`, and `GND`. The
silkscreen matches the common YD-ESP32-S3/HiLetgo-style layout documented in
the [ESP32-S3 N16R8 pinout manual](https://manuals.plus/asin/B0CDWXWXCG.pdf).
The exact seller SKU is not printed clearly enough to claim a manufacturer
identity, but the **header labels in the photos are now verified**.

Important N16R8 restrictions remain: GPIO35-37 are connected to octal PSRAM;
GPIO19-20 are native USB D-/D+; GPIO43-44 serve the `COM` USB-UART bridge;
GPIO0, 3, 45, and 46 are boot-strapping pins. The existing firmware still
deliberately leaves physical LCD and GPS GPIOs at `-1`; this document does not
enable them.

### TFT

The marking `2.8 TFT SPI 240X320 V1.2`, red PCB, 14-pin display/touch header,
and separate four-pad SD header match the common LCDWIKI MSP2806/MSP2807-style
module. Its documentation identifies an **ILI9341** controller, 240×320 RGB TFT,
and four-wire SPI interface. The same family documents the display header as
VCC, GND, CS, RESET, DC, SDI(MOSI), SCK, LED, SDO(MISO), followed by touch pins.

This is a strong family match, not a serial-number-level identification of
Simon’s exact board. The exact vendor/SKU and component population still need a
clear back-side photograph.

The supplied wiring-photo PDF shows the `J1` area as **open**, as confirmed by
Simon. Treat that as the working power state for this handoff: TFT `VCC` is fed
from the ESP32 `5V` header and TFT `LED` is fed from the ESP32 `3V3` header.

Sources:

- [LCDWIKI MSP2807 manual](https://www.lcdwiki.com/res/MSP2807/2.8inch_SPI_Module_MSP2807_User_Manual_EN.pdf) — ILI9341, 240×320, four-wire SPI, header functions.
- [LCDWIKI MSP2806 outline](https://www.lcdwiki.com/res/MSP2807/MSP2806_Size_QD.pdf) — V1.2 marking, pin numbering, ILI9341V and SPI-4-line notes.
- [DIYmalls manual for the exact V1.2 marking](https://manuals.plus/asin/B0D8BLFJXG.pdf) — J1 power selection and 3.3 V TTL logic notes.

## 2. Power and J1

The matching V1.2 manual states:

- **J1 open:** VCC expects 5 V and the onboard regulator supplies the display
  rail. This is Simon’s reported state for the board in hand.
- **J1 closed:** VCC expects 3.3 V and the regulator is bypassed.
- SPI/control signals are 3.3 V logic; the ESP32-S3’s 3.3 V GPIO levels are the
  correct logic domain.
- LED is a backlight control input; the manual says connecting it to 3.3 V
  gives an always-on backlight. Connect it to the ESP32 `3V3` power header,
  **not to a GPIO**.

J1 is reported open for this board. If that reading is uncertain, measure the
pad-to-pad continuity with USB power disconnected:

- open circuit / no beep: J1 open;
- near-zero resistance / beep: J1 closed.

Do not connect TFT VCC to ESP32 `3V3` while J1 is open. Do not connect LED to an
ESP32 GPIO; use the `3V3` power header as specified by the module manual.

## 3. Wiring table — final for the first power test

The ESP32 signal assignments below are conservative choices from the verified
header silkscreen. GPIO10/11/12 are the board's conventional SPI CS/MOSI/SCK
group; GPIO4/5 are ordinary exposed GPIOs for display reset and D/C. These are
**wiring recommendations only** until the final continuity check is done.

| TFT pin | ESP32 header label | ESP32 GPIO | Wire type | Notes |
|---|---|---:|---|---|
| VCC | `5V` | `5V` power header | Female-to-male jumper | J1 open: onboard regulator expects 5V |
| GND | `GND` | `GND` header pin | Female-to-male jumper | Use either verified ESP GND header pin |
| CS | `10` | GPIO10 | Female-to-male jumper | Active-low TFT chip select |
| RESET | `4` | GPIO4 | Female-to-male jumper | Active-low display reset |
| DC | `5` | GPIO5 | Female-to-male jumper | Data/command select |
| SDI(MOSI) | `11` | GPIO11 | Female-to-male jumper | SPI controller-to-display data |
| SCK | `12` | GPIO12 | Female-to-male jumper | SPI clock |
| LED | `3V3` | `3V3` power header | Female-to-male jumper | Always-on backlight per module manual; do not use a GPIO |
| SDO(MISO) | Not connected | N/A | None | Safe to omit for write-only color-bar test, per module manual |

The provisional ESP-IDF host for the future driver is **SPI2 / host 1**, because
that is the host exposed by the existing ST7565 configuration. This is not a
firmware change or a claim that the driver is ready.

## 4. Keep these pins disconnected

Do not connect any of the following for the first display test:

- Touch: `T_CLK`, `T_CS`, `T_DIN`, `T_DO`, `T_IRQ`.
- microSD: `SD_CS`, `SD_MOSI`, `SD_MISO`, `SD_SCK`.
- TFT `SDO(MISO)` unless a later readback test explicitly needs it.

At the ESP32 side, preserve native USB pins, flash/PSRAM-connected pins,
boot-strapping pins, and the future GPS UART. The earlier provisional suggestion
of GPIO 8/9/10/11/12 has been narrowed to the approved signal set in the table:
GPIO10/11/12 plus GPIO4/5.

## 5. Physical materials

- One breadboard is optional; it is useful only if the TFT header and ESP32
  headers cannot sit side-by-side.
- Use female-to-male Dupont leads when the TFT has a male header and the ESP32
  exposes male headers. Use female-to-female only if both boards expose male
  pins and you are using a separate male-header breakout; do not force a loose
  wire onto a bare pad.
- No level shifter should be needed for ESP32-S3 3.3 V logic. With J1 open, use
  the TFT `5V` input shown in the final table.
- Do not add a custom PCB yet. First verify the TFT J1 state and complete the
  continuity checks below.

## 6. Multimeter checks before any power

With **both USB power and any external supply disconnected**:

1. The supplied PDF already provides the ESP32 front/back photos and header
   labels; use it as the pinout reference.
2. The supplied PDF shows the TFT header and J1 area; use it as the reference.
3. Confirm the TFT header is actually soldered and identify pin 1 from the
   silkscreen. Do not infer left/right from a mirrored back view.
4. If the reported J1 state is uncertain, measure its continuity as described
   above.
5. Check continuity from each selected ESP32 header pin to its silkscreen pad;
   do not use GPIO numbers alone.
6. Check for a short between TFT VCC and GND. Do not proceed if the reading is
   a hard short.
7. Check that the candidate ESP 3V3 and GND pins are the pins named by the
   carrier silkscreen, not merely adjacent pins.

## 7. First-power procedure (J1 reported open)

1. Disconnect USB from the ESP32.
2. Leave TFT touch, SD, and SDO disconnected; connect LED in step 5.
3. Confirm J1 is open and use the final table: VCC to `5V`, LED to `3V3`.
4. Connect TFT GND to the approved ESP GND pin first.
5. Connect TFT VCC to ESP32 `5V` and TFT LED to ESP32 `3V3`.
6. Connect CS, RESET, DC, MOSI, and SCK one wire at a time.
7. Recheck for power-to-ground shorts and accidental adjacent-header bridges.
8. Apply power through USB only after the checks pass. Do not connect another
   power supply to the ESP32 at the same time.
9. A lit backlight, white screen, or black screen is **not** proof of SPI
   communication; the display driver is not installed yet.

## 8. Future GPS reservation

No GPS GPIOs are approved yet. The firmware Kconfig exposes UART number, RX,
and TX as `-1 until verified`; keep them unassigned until the ESP carrier photo
and the GPS module header are available. The TFT plan must not consume the
future UART pins.

## 9. Firmware handoff for the next milestone

After Simon confirms the wiring and the first power check:

- add an ILI9341 SPI driver on SPI2/host 1;
- configure 320×240 landscape and RGB565;
- use the approved CS/RESET/DC/MOSI/SCK GPIOs;
- initially leave MISO, touch, and SD unused;
- drive the backlight only through the verified LED/current path;
- start with a color-bar test, then text, then the BikeOS map.

No such configuration is enabled by this document.

## Instructions for Simon — Wire These Connections

The ESP32 signal and power sides are now mapped. With both boards unpowered,
connect these wires:

1. TFT `GND` -> ESP32 `GND`.
2. TFT `VCC` -> ESP32 `5V`.
3. TFT `LED` -> ESP32 `3V3`.
4. TFT `CS` -> ESP32 GPIO10.
5. TFT `RESET` -> ESP32 GPIO4.
6. TFT `DC` -> ESP32 GPIO5.
7. TFT `SDI(MOSI)` -> ESP32 GPIO11.
8. TFT `SCK` -> ESP32 GPIO12.
9. Leave TFT `SDO(MISO)`, touch, and SD pins disconnected.

Before plugging in USB, check that there is no short between TFT `VCC` and
`GND`, and that the `5V` and `3V3` wires are not swapped. Use only the ESP32
USB cable as the power source for this first test.
