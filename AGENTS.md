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

Provides platform adapter `eink_voice_agent` (WebSocket server on `:8123` for device conversations) and tools: `build_firmware`, `flash_firmware`, `monitor_device`, `ci_status`.

## Key Decisions
- ES8311 audio codec (I2C+I2S) on Waveshare ESP32-C6-ePaper-1.54 — pin mapping in `app_config.h` needs verification against schematic
- ULP RISC-V removed — not available in IDF v5.3 for ESP32-C6; uses timer+GPIO deep sleep instead
- e-paper uses `tuanpmt/esp_epaper` framebuffer + 5×7 bitmap font (no LVGL UI)
- **MFE is NOT in-graph**: TFLite model input shape is [1, 3960] (99 frames × 40 mel bins), not 16000 raw PCM. MFE extraction runs separately in C before inference
- Model uses `espressif/esp-tflite-micro` + `espressif/esp-nn` from ESP registry

## Goal
- Integrate Edge Impulse wake word model into the eink-voice-agent firmware

## Constraints & Preferences
- Edge Impulse model is a trigger word model (`"hi_jeff"`/`"noise"`/`"unknown"`) using MobileNetV2 0.35 (int8 quantized TFLite)
- MFE extracted separately: 99 frames × 40 filters from 16000 PCM at 16kHz (20ms frame, 10ms stride)
- TFLite model takes 3960 int8 features as input (scale=0.00390625, zp=-128)
- ESP32-C6 has no PSRAM support; model stored in flash (615KB C array), 162KB arena from heap (DRAM)
- ESP-IDF v6.0.1 is the build environment

## Progress
### Done
- WebSocket disconnect fix: Hermes adapter processes audio in `asyncio.create_task` background coroutine
- I2S audio capture fix: TX channel enabled permanently at init
- Button wake from deep sleep: `esp_sleep_enable_ext1_wakeup` (GPIO2 only)
- Boot loop fix: playback buffer reduced to 3s, DMA frame clamped to 2046
- Firmware: WS reconnect watchdog every 5s in main loop
- **TFLite Micro integration**: `wake_word.cpp` rewritten with 16000-sample buffer, MFE preprocessing (256-point FFT, Mel filterbank, log-compression), and TFLite inference
- Precomputed MFE tables: Hann window (256), Mel filterbank (40 × 129)
- Model binary (615KB) converted to C array in `models/tflite_learn_1037720_5.c`
- Dependencies added: `espressif/esp-tflite-micro` v1.3.7, `espressif/esp-nn` v1.2.3
- **ES8311 audio fix (ADC)**: `0x2A` set to `0x7C` — enables MIC1L input to ADC (was disabled, bit 3 = 0 in old `0x74`)
- **ES8311 format fix**: `0x06`/`0x16` changed from `0x70` (left-justified) to `0x00` (Philips I2S) — now matches I2S master Philips format (`bit_shift=true`)
- **Renamed device**: `Merlin` → `Jeff`, wake word `"hey merlin"` → `"hi jeff"`
- Build verified: firmware compiles successfully
- **SD card fixes**: `format_if_mount_failed=false`, `allocation_unit_size=512`, enhanced rebuild_index diagnostics
- **Whisper transcription fix**: Removed unsupported `--no_timestamps` arg, added explicit `--output_format txt`
- **LED control**: Added `board_power_led_on/off()` in `system_init.h/c`, LED turns on during recording modes, off during sleep and when returning home
- **Settings menu**: Added dedicated settings screen with WiFi IP, Battery, Storage, and Manual Sync options (moved from main menu Sync item)

### Blocked
- (none)

## Key Decisions
- Implemented MFE in C (256-point FFT, Hann window, Mel filterbank, log compression) — no edge-impulse-sdk needed
- Model stored as C array (xxd-style) — no INCBIN dependency
- 7 TFLite ops registered: Reshape, Conv2D, DepthwiseConv2D, Add, FullyConnected, Softmax, Mean
- File renamed `wake_word.c` → `wake_word.cpp` (needs C++ for TFLite Micro API)
- **Processing flag timing fix**: Set `processing=true` BEFORE stopping recording to prevent wake word task from interrupting
- **Wake word loop fix**: Added `processing` check to wake word inner loop to break early if transitioning
- **Battery display fix**: `draw_status_bar()` and `ui_show_docked_screen()` now read live battery percentage instead of stale static value
- **LED control**: Single pin (EXIO_LED on TCA9554 bit 4) turns on during recording, off during sleep/home
- **Settings menu**: Replaced "Sync" menu item with "Settings" submenu containing WiFi IP, Battery, Storage, and Manual Sync
- **Thinking animation**: Replaced pulsing square with rotating dot animation (circular pattern)

## Next Steps
1. Flash updated firmware and test full transcription flow
2. Verify wake word detection with "hi jeff"
3. Test docked state shows correctly when plugged in
4. Verify LED behavior during recording/sleep/home transitions

1. **Testing**: Audio capture working (14 packets sent), transcription returning short text ("You"). Wait for DMA buffer update to improve capture timing.
2. **Tune MFE normalization**: The current implementation normalizes per-frame: (db_val - noise_floor) / (max_db - noise_floor). Verify this matches Edge Impulse's expected input distribution. May need per-file min/max normalization instead.

## Critical Context
- Model input tensor: `serving_default_x:0` int8 [1, 3960], scale=0.00390625, zp=-128
- Model output tensor: `StatefulPartitionedCall:0` int8 [1, 3], scale=0.00390625, zp=-128
- **Arena size**: 130284 bytes (static BSS) + audio_buf (32KB)
- Audio pipeline feeds 800-sample chunks; wake_word.cpp buffers internally to 16000
- MFE parameters: 20ms frame (320 samples), 10ms stride (160 samples), 40 mel filters, 256-pt FFT, noise floor -52 dB
- Classes: "hi_jeff" (index 0), "noise" (1), "unknown" (2) — sensitivity threshold 0.7
- SD card mount config: `allocation_unit_size=512`, `format_if_mount_failed=false`, `max_files=8`

## Relevant Files
- `firmware/main/audio/wake_word.cpp` — MFE + TFLite Micro inference (16000-sample buffer)
- `firmware/main/audio/wake_word.h` — C API header with extern "C"
- `firmware/main/models/tflite_learn_1037720_5.c` — model binary as C array (615KB)
- `firmware/main/models/tflite_learn_1037720_5.h` — model header with arena size
- `firmware/main/models/model_mfe.h` — precomputed Hann window + Mel filterbank (116KB)
- `firmware/main/models/model_metadata.h` — input/output sizes and class indices
- `firmware/main/models/model_ops.h` — TFLite Micro op resolver setup
- `firmware/main/idf_component.yml` — dependencies
- `firmware/main/CMakeLists.txt` — build config (includes models dir, wake_word.cpp)
- `firmware/main/audio/audio_pipeline.c` — existing pipeline (no changes needed)
- `model/tflite-model/` — Edge Impulse exported model files (original)
- `model/model-parameters/` — Edge Impulse metadata (for reference)
