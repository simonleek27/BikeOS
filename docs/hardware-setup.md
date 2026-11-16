# Hardware setup

## ESP32 prototype

The prototype was queried through the Espressif ROM bootloader on 16 November
2026. The electrical configuration below is verified; the exact carrier-board
SKU is not yet verified from silkscreen.

| Property | Verified value | Verification method |
|---|---|---|
| SoC | ESP32-S3 QFN56 revision 0.2 | `esptool chip-id` |
| Flash | 16 MB external quad, 3.3 V | `esptool flash-id` |
| PSRAM | 8 MB embedded, 3.3 V | ESP ROM feature report |
| USB | USB Serial/JTAG | ESP ROM feature report and macOS USB enumeration |
| macOS port | `/dev/cu.usbmodem1101` during bring-up | `arduino-cli board list` |

The exact development-board/module SKU must be read from the module metal can
and carrier-board silkscreen. Do not copy a DevKitC-1 pin map until that is done.

## LCD status: do not wire yet

The repository architecture names Adafruit product 4694 / Sharp LS027B7DH01,
but the screen physically on hand has not yet been verified against that model.
No GPIO assignments are approved yet.

Before wiring, provide clear photos of:

1. The front and back of the complete display assembly.
2. Every header/connector label.
3. Any product sticker, PCB revision, or panel part number.
4. The ESP32 module and carrier-board silkscreen.

After the model is confirmed, this document will contain the final wiring table
for power, ground, SPI clock/data/chip-select, display enable, and the required
Sharp Memory LCD polarity/VCOM signal. Connect wires only with USB/power removed.

### Conditional reference for the planned display

If the photos confirm **Adafruit product 4694 / Sharp LS027B7DH01**, the
manufacturer documentation establishes these constraints:

- 400 × 240 monochrome pixels, 58.8 × 35.28 mm active area.
- Write-only serial/SPI-style interface; a full framebuffer is 12,000 bytes.
- The Adafruit breakout uses `VIN`, `GND`, `CLK`, `DI`, and active-high `CS` for
  normal operation. The breakout accepts 3 V or 5 V power/logic.
- The 400 × 240 panel clock is limited to 2 MHz.
- The panel has no usable backlight; night use needs external front lighting.

Primary references:

- [Adafruit product 4694](https://www.adafruit.com/product/4694)
- [Adafruit Sharp Memory Display guide](https://learn.adafruit.com/adafruit-sharp-memory-display-breakout)
- [Sharp LS027B7DH01 datasheet](https://cdn-learn.adafruit.com/assets/assets/000/094/215/original/LS027B7DH01_Rev_Jun_2010.pdf)

These are not yet evidence that the physical screen on the bench is that exact
breakout. Do not use this conditional information as a wiring instruction.

