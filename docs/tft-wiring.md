# BikeOS 2.8-inch TFT wiring handoff

**Status: PROVISIONAL — do not connect power yet.**

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

The **carrier-board model, header labels, and exposed pins are not verified**.
The chip identity is not enough to authorize a header-to-GPIO connection. The
existing firmware deliberately leaves physical LCD and GPS GPIOs at `-1` until
the carrier is identified.

### TFT

The marking `2.8 TFT SPI 240X320 V1.2`, red PCB, 14-pin display/touch header,
and separate four-pad SD header match the common LCDWIKI MSP2806/MSP2807-style
module. Its documentation identifies an **ILI9341** controller, 240×320 RGB TFT,
and four-wire SPI interface. The same family documents the display header as
VCC, GND, CS, RESET, DC, SDI(MOSI), SCK, LED, SDO(MISO), followed by touch pins.

This is a strong family match, not a serial-number-level identification of
Simon’s exact board. The exact vendor/SKU and component population still need a
clear back-side photograph.

Sources:

- [LCDWIKI MSP2807 manual](https://www.lcdwiki.com/res/MSP2807/2.8inch_SPI_Module_MSP2807_User_Manual_EN.pdf) — ILI9341, 240×320, four-wire SPI, header functions.
- [LCDWIKI MSP2806 outline](https://www.lcdwiki.com/res/MSP2807/MSP2806_Size_QD.pdf) — V1.2 marking, pin numbering, ILI9341V and SPI-4-line notes.
- [DIYmalls manual for the exact V1.2 marking](https://manuals.plus/asin/B0D8BLFJXG.pdf) — J1 power selection and 3.3 V TTL logic notes.

## 2. Power and J1

The matching V1.2 manual states:

- **J1 open:** VCC expects 5 V and the onboard regulator supplies the display
  rail.
- **J1 closed:** VCC expects 3.3 V and the regulator is bypassed.
- SPI/control signals are 3.3 V logic; the ESP32-S3’s 3.3 V GPIO levels are the
  correct logic domain.
- LED is a backlight control input; the manual says a fixed high level lights
  it. The backlight current path on Simon’s board has not been verified.

Do not rely on the default J1 state. A photograph must show whether the two J1
pads are bridged by solder. If the photo is unclear, measure the pad-to-pad
continuity with USB power disconnected:

- open circuit / no beep: J1 open;
- near-zero resistance / beep: J1 closed.

Until that check is complete, **do not connect VCC or LED**. Do not connect LED
directly to an ESP32 GPIO; use the board’s verified current-limited path or a
separate transistor/MOSFET path in the future.

## 3. Wiring table — not yet approved

The TFT-side functions are known, but the required ESP32 header labels and GPIO
numbers are not. This table must remain unpopulated until the carrier-board
photographs are reviewed.

| TFT pin | ESP32 header label | ESP32 GPIO | Wire type | Notes |
|---|---|---:|---|---|
| VCC | **TBD — do not connect** | **TBD** | Power | J1 state and carrier 3V3/5V/VBUS labels must be verified first |
| GND | **TBD — do not connect** | **TBD** | Ground | Use a verified ESP GND header pin |
| CS | **TBD** | **TBD** | Female-to-male jumper | Active-low TFT chip select |
| RESET | **TBD** | **TBD** | Female-to-male jumper | Active-low display reset |
| DC | **TBD** | **TBD** | Female-to-male jumper | Data/command select |
| SDI(MOSI) | **TBD** | **TBD** | Female-to-male jumper | SPI controller-to-display data |
| SCK | **TBD** | **TBD** | Female-to-male jumper | SPI clock |
| LED | **TBD — leave disconnected** | **TBD** | None for first inspection | Backlight path/current must be verified |
| SDO(MISO) | Not connected | N/A | None | Safe to omit for write-only color-bar test, per module manual |

The provisional ESP-IDF host for the future driver is **SPI2 / host 1**, because
that is the host exposed by the existing ST7565 configuration. This is not a
pin assignment. The next firmware task must add a separate ILI9341 configuration
after the physical header map is approved.

## 4. Keep these pins disconnected

Do not connect any of the following for the first display test:

- Touch: `T_CLK`, `T_CS`, `T_DIN`, `T_DO`, `T_IRQ`.
- microSD: `SD_CS`, `SD_MOSI`, `SD_MISO`, `SD_SCK`.
- TFT `SDO(MISO)` unless a later readback test explicitly needs it.

At the ESP32 side, do not select pins until the carrier photo is reviewed. In
particular, preserve native USB pins, flash/PSRAM-connected pins, boot-strapping
pins, and the future GPS UART. The earlier provisional suggestion of GPIO
8/9/10/11/12 is **not approved**.

## 5. Physical materials

- One breadboard is optional; it is useful only if the TFT header and ESP32
  headers cannot sit side-by-side.
- Use female-to-male Dupont leads when the TFT has a male header and the ESP32
  exposes male headers. Use female-to-female only if both boards expose male
  pins and you are using a separate male-header breakout; do not force a loose
  wire onto a bare pad.
- No level shifter should be needed for ESP32-S3 3.3 V logic, but the TFT VCC
  selection still must be verified.
- Do not add a custom PCB yet. First identify the carrier and the TFT J1 state.

## 6. Multimeter checks before any power

With **both USB power and any external supply disconnected**:

1. Photograph the entire front and back of the ESP32 board, including every
   header label and the USB connector.
2. Photograph the entire back of the TFT and a close-up of J1, component labels,
   and the 14-pin header orientation.
3. Confirm the TFT header is actually soldered and identify pin 1 from the
   silkscreen. Do not infer left/right from a mirrored back view.
4. Measure J1 continuity as described above.
5. After a candidate carrier pin map is supplied, check continuity from each
   candidate header pin to its silkscreen pad; do not use GPIO numbers alone.
6. Check for a short between TFT VCC and GND. Do not proceed if the reading is
   a hard short.
7. Check that the candidate ESP 3V3 and GND pins are the pins named by the
   carrier silkscreen, not merely adjacent pins.

## 7. First-power procedure (blocked pending photographs)

1. Disconnect USB from the ESP32.
2. Leave TFT touch, SD, LED, and SDO disconnected.
3. Verify J1 state and the approved VCC voltage from the final table.
4. Connect TFT GND to the approved ESP GND pin first.
5. Connect TFT VCC only after the final table says 3V3 or 5V for this J1 state.
6. Connect CS, RESET, DC, MOSI, and SCK one wire at a time.
7. Recheck for power-to-ground shorts and accidental adjacent-header bridges.
8. Apply power through USB only after the checks pass.
9. A lit backlight, white screen, or black screen is **not** proof of SPI
   communication; the display driver is not installed yet.

## 8. Future GPS reservation

No GPS GPIOs are approved yet. The firmware Kconfig exposes UART number, RX,
and TX as `-1 until verified`; keep them unassigned until the ESP carrier photo
and the GPS module header are available. The TFT plan must not consume the
future UART pins.

## 9. Firmware handoff for the next milestone

After Simon confirms the wiring and the exact carrier map:

- add an ILI9341 SPI driver on SPI2/host 1;
- configure 320×240 landscape and RGB565;
- use the approved CS/RESET/DC/MOSI/SCK GPIOs;
- initially leave MISO, touch, and SD unused;
- drive the backlight only through the verified LED/current path;
- start with a color-bar test, then text, then the BikeOS map.

No such configuration is enabled by this document.

## Instructions for Simon — Wire These Connections

**Do not wire or power the TFT yet.** I still need these photographs:

1. ESP32 board: sharp front and back photos showing the complete board,
   silkscreen, headers, USB connector, and any board name/model.
2. TFT: sharp back photo showing the full red PCB, the `J1` area, all component
   labels, and the header orientation; include a close-up of J1.

Once those are available, I will replace every `TBD` in the table with the
actual ESP32 header label and GPIO number, confirm whether VCC is 3V3 or 5V,
and give you a one-wire-at-a-time connection list. Until then, leave USB
disconnected from the ESP32 and do not connect the TFT.
