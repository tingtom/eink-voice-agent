# Hardware Details — Waveshare ESP32-C6 e-Paper

## Board Specifications

| Spec | Detail |
|------|--------|
| **Chip** | ESP32-C6FH4 (RISC-V 32-bit single-core, up to 160MHz) |
| **RAM** | 512KB HP SRAM + 16KB LP SRAM |
| **ROM** | 320KB |
| **Flash** | External (board-specific, typically 4-8MB) |
| **Display** | 1.54" e-Paper, 200×200, B/W |
| **Wireless** | Wi-Fi 6 (802.11ax), Bluetooth 5, IEEE 802.15.4 (Zigbee/Thread) |
| **USB** | USB-C (programming + power) |
| **GPIO** | Available via header pins |

## Key Peripherals for This Project

### I2S Audio (Microphone)
- ESP32-C6 has I2S peripheral — needed for INMP441 MEMS microphone
- I2S clock (BCLK), word select (LRCK), data (SD) pins required
- PDM microphone support also available

### SPI (e-Paper Display)
- Display connected via SPI — handled by Waveshare SDK
- BUSY, RST, DC, CS pins for display control

### GPIO (Buttons)
- Need 4 GPIO pins for navigation buttons
- Internal pull-ups available, connect buttons to GND

### I2S (Audio Output)
- Second I2S channel or shared bus for MAX98357A I2S amplifier
- Drives small speaker for TTS feedback

## Pin Mapping (Planned)

| Function | GPIO | Notes |
|----------|------|-------|
| I2S BCLK | GPIO? | Mic clock |
| I2S LRCK | GPIO? | Mic word select |
| I2S DATA | GPIO? | Mic data in |
| I2S OUT | GPIO? | Speaker data out |
| E-PAPER BUSY | ? | Display busy signal |
| E-PAPER RST | ? | Display reset |
| E-PAPER DC | ? | Data/Command select |
| E-PAPER CS | ? | SPI chip select |
| E-PAPER CLK | ? | SPI clock |
| E-PAPER DIN | ? | SPI data in |
| BUTTON_UP | ? | Internal pull-up |
| BUTTON_DOWN | ? | Internal pull-up |
| BUTTON_SELECT | ? | Internal pull-up |
| BUTTON_BACK | ? | Internal pull-up |
| BATTERY_ADC | ? | Battery voltage sense |
| CHARGE_STATUS | ? | TP4056 status pin |

*Note: Exact pin assignments to be confirmed from Waveshare schematic and ESP32-C6 datasheet.*

## Power Budget

| Component | Active | Sleep |
|-----------|--------|-------|
| ESP32-C6 | ~80mA | ~5µA (deep sleep) |
| e-Paper refresh | ~25mA (brief) | 0 (bistable) |
| INMP441 mic | ~1.4mA | ~0 (power off) |
| MAX98357A amp | ~20mA (playing) | ~0 (shutdown) |
| WiFi TX | ~240mA peak | 0 |

**Target battery life:** 1-2 weeks with moderate use (deep sleep between interactions, wake on button/voice)

## Links

- [[Project Overview]]
- [[Firmware Plan]]
- [[Bill of Materials]]
