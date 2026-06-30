# CAD Design - E-Ink Voice Agent

This directory contains CAD files for the device case and any custom PCB designs.

## Reference: Waveshare ESP32-C6-ePaper-1.54

The device uses the [Waveshare ESP32-C6-ePaper-1.54](https://www.waveshare.com/wiki/ESP32-C6-ePaper-1.54) board as the base. All modifications are documented here.

### Board Specifications
- **Dimensions:** 68mm x 35mm
- **Display:** 1.54" e-Paper, 200×200 pixels (24mm visible area)
- **MCU:** ESP32-C6FH4 (RISC-V, single-core, up to 160MHz)
- **USB-C:** Programming + power (on left edge)

## Files

| File | Description |
|------|-------------|
| `case.scad` | Parametric OpenSCAD case design |
| `dimensions.dxf` | Board outline DXF |
| `pinout.txt` | GPIO pin mapping reference |
| `library.lib` | KiCad symbol library stubs |

## Modifications to Reference Board

The Waveshare board is used as-is. Additional components:

| Component | GPIO | Connection |
|-----------|------|------------|
| INMP441 Mic (I2S) | 19,21,22,20 | MCLK, BCLK, WS, DIN |
| MAX98357A Amp (I2S) | 19,21,22,23 | MCLK, BCLK, WS, DOUT |
| Buttons | 9, 2 | Boot button (PWR), GPIO2 |
| SD Card | 3,5,4,6 | CS, MOSI, MISO, SCK (shared SPI) |

## Pin Mapping (from app_config.h)

| Function | GPIO |
|----------|------|
| I2C SDA | 18 |
| I2C SCL | 8 |
| EPD MOSI | 5 |
| EPD MISO | 4 |
| EPD CLK | 6 |
| EPD CS | 7 |
| EPD DC | 15 |
| EPD RST | 11 |
| EPD BUSY | 10 |
| I2S MCLK | 19 |
| I2S BCLK | 21 |
| I2S WS | 22 |
| I2S DIN | 20 |
| I2S DOUT | 23 |
| SD CS | 3 |
| Button Boot | 9 |
| Button Power | 2 |

## Case Design Notes

The OpenSCAD case (`case.scad`) is parametric:

- `board_length` / `board_width` — Board footprint (68mm x 35mm)
- `display_size` — Visible display area (24mm x 24mm)
- `button_diameter` / `button_2x2_positions` — Button cutouts
- `speaker_diameter` — Speaker opening (18mm)
- `mic_grille_diameter` — Microphone opening

To render and export STL files:
```bash
openscad -o case_body.stl case.scad
openscad -o case_lid.stl -D 'render()' -D 'module case_lid()' case.scad
# Or open in OpenSCAD GUI and export as STL
```

**Note:** Dimensions should be verified against actual board once received. The Waveshare wiki has mechanical drawings that may differ from estimates.

## Manufacturing

Estimated volume: ~40cm³
Recommended: 3D print in PETG or ABS for durability
Layer height: 0.2mm
Infill: 20%