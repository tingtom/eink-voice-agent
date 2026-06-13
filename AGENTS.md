# Agent Setup

## Build Environment
- ESP-IDF at `/opt/esp-idf` (v5.4)
- CI runs `espressif/idf:v5.3` Docker — API differences possible
- All IDF managed components already under `firmware/managed_components/`

## Build Commands
```bash
. /opt/esp-idf/export.sh
cd /root/eink-voice-agent/firmware
idf.py build
```

## Flash (when hardware arrives)
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Project Structure
- `firmware/main/` — all application code
- `firmware/main/network/provisioning.c` — SoftAP `EInk-Voice-Config` + HTTP config at `http://192.168.4.1`
- `firmware/main/app_config.h` — all pins, WiFi, server URLs, thresholds
- `.github/workflows/build.yml` — CI using `espressif/idf:v5.3`

## Hermes Plugin

```bash
hermes plugins install /root/eink-voice-agent/.hermes/plugins/eink-voice-agent
```

Provides tools: `build_firmware`, `flash_firmware`, `monitor_device`, `ci_status`.

## Key Decisions
- ES8311 audio codec (I2C+I2S) on Waveshare ESP32-C6-ePaper-1.54 — pin mapping in `app_config.h` needs verification against schematic
- ULP RISC-V removed — not available in IDF v5.3 for ESP32-C6; uses timer+GPIO deep sleep instead
- Wake word stub in `audio/wake_word.c` — needs Edge Impulse TFLite model trained and loaded
- e-paper uses `tuanpmt/esp_epaper` framebuffer + 5×7 bitmap font (no LVGL UI)
