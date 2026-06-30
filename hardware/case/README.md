# CAD Design - E-Ink Voice Agent

Parametric 3D printable case for Waveshare ESP32-C6-ePaper-1.54.

## Board Specifications

- **Board:** Waveshare ESP32-C6-ePaper-1.54 (used as-is)
- **Dimensions:** 68mm x 35mm
- **Display:** 1.54" e-Paper, 200×200 pixels (24mm visible area)
- **MCU:** ESP32-C6FH4 (RISC-V, single-core, up to 160MHz)
- **USB-C:** Programming + power on left edge

## Files

| File | Description |
|------|-------------|
| `case.scad` | Parametric OpenSCAD source |
| `case_body.stl` | Exported mesh (ready for Fusion 360) |

## Using in Fusion 360

File → Import → STL to open `case_body.stl` directly, or edit `case.scad` in OpenSCAD and re-export.

## Case Parameters

Adjust in `case.scad`:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `board_length` | 68 | Board X dimension (mm) |
| `board_width` | 35 | Board Y dimension (mm) |
| `display_size` | [24,24] | Display window (mm) |
| `button_diameter` | 6 | Button cutout diameter (mm) |
| `button_spacing_x/y` | 14/10 | Button grid spacing (mm) |
| `speaker_diameter` | 18 | Speaker opening (mm) |
| `mic_grille_diameter` | 8 | Microphone opening (mm) |
| `wall` | 1.5 | Case wall thickness (mm) |

## Manufacturing

- Volume: ~40cm³
- Material: PETG or ABS
- Layer height: 0.2mm
- Infill: 20%

**Note:** Verify dimensions against actual board once received.