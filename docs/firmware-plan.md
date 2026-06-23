# Firmware Plan — E-Ink Voice Agent

## Framework Choice: ESP-IDF

**Why ESP-IDF over Arduino:**
- Better I2S driver support for audio input/output
- Native FreeRTOS for task management (audio, display, WiFi in parallel)
- Better power management (deep sleep, light sleep)
- First-class Edge Impulse integration
- More control over SPI display timing

## Firmware Architecture

```
main/
├── CMakeLists.txt
├── main/
│   ├── main.c                  # Entry point, system init
│   ├── app_config.h            # Pin definitions, WiFi creds, API endpoints
│   ├── system/
│   │   ├── system_init.c       # NVS, GPIO, power management
│   │   ├── system_init.h
│   │   ├── power_mgmt.c        # Deep sleep, battery monitoring
│   │   └── power_mgmt.h
│   ├── audio/
│   │   ├── mic_driver.c        # I2S microphone capture
│   │   ├── mic_driver.h
│   │   ├── audio_pipeline.c    # Wake word → record → stream
│   │   ├── audio_pipeline.h
│   │   ├── wake_word.c         # Edge Impulse TFLite model
│   │   ├── wake_word.h
│   │   ├── speaker_driver.c    # I2S audio output (TTS playback)
│   │   └── speaker_driver.h
│   ├── display/
│   │   ├── epaper_driver.c     # Waveshare e-paper driver
│   │   ├── epaper_driver.h
│   │   ├── ui_manager.c        # Screen layouts, menus, text rendering
│   │   ├── ui_manager.h
│   │   ├── font_small.c        # Bitmap fonts for 200x200
│   │   ├── font_medium.c
│   │   └── font_large.c
│   ├── input/
│   │   ├── buttons.c           # GPIO button handling, debounce
│   │   └── buttons.h
│   ├── network/
│   │   ├── wifi_manager.c      # WiFi connect, reconnect, AP fallback
│   │   ├── wifi_manager.h
│   │   ├── ws_client.c         # WebSocket client for Hermes
│   │   ├── ws_client.h
│   │   ├── http_client.c       # HTTP API calls
│   │   └── http_client.h
│   ├── modes/
│   │   ├── mode_voice_agent.c  # Voice conversation with Hermes
│   │   ├── mode_voice_agent.h
│   │   ├── mode_note.c         # Voice note → Hermes → Obsidian
│   │   ├── mode_note.h
│   │   ├── mode_transcribe.c   # Record → Hermes Whisper → display
│   │   ├── mode_transcribe.h
│   │   ├── mode_games.c        # Text games (trivia, adventure)
│   │   ├── mode_games.h
│   │   ├── mode_dashboard.c    # HA status, weather, calendar
│   │   └── mode_dashboard.h
│   └── utils/
│       ├── ringbuffer.c        # Audio ring buffer
│       ├── ringbuffer.h
│       ├── base64.c            # Audio encoding for WebSocket
│       └── base64.h
├── components/                  # Third-party libraries
│   ├── edge-impulse/           # TFLite wake word model
│   └── waveshare-epaper/       # E-paper driver
├── models/                      # Edge Impulse model data
│   └── wake_word_model.h
├── partitions.csv              # Custom partition table
└── sdkconfig.defaults
```

## Task Priorities (FreeRTOS)

| Task | Priority | Core | Purpose |
|------|----------|------|---------|
| Audio capture | High | 0 | I2S DMA → ring buffer |
| Wake word | High | 0 | Process audio, detect keyword |
| Display | Medium | 1 | UI updates, partial refresh |
| Network | Medium | 1 | WiFi, WebSocket, HTTP |
| Buttons | Low | 1 | GPIO polling, debounce |
| Power mgmt | Low | 1 | Battery monitor, sleep decision |

## Audio Pipeline Flow

```
INMP441 → I2S DMA → Ring Buffer
                              ↓
                    ┌── Wake Word Task (Edge Impulse)
                    │   (always running, low power)
                    │
                    ↓ (keyword detected)
                    ┌── Recording Task
                    │   Stream audio chunks via WebSocket
                    │   to Hermes /api/voice/stream
                    │
                    ↓ (Hermes responds)
                    ┌── Playback Task
                    │   Receive TTS audio from Hermes
                    │   I2S → MAX98357A → Speaker
                    │
                    ↓ (done)
                    Return to wake word listening
```

## Wake Word Training (Edge Impulse)

1. Collect audio samples of wake phrase (e.g., "Hi Jeff" or custom)
2. Train keyword spotting model in Edge Impulse
3. Export as TFLite model for ESP-IDF
4. Target: <100ms inference, <50KB model size
5. False accept rate <1 per hour of ambient noise

## Display UI Screens

### Idle Screen
```
┌──────────────────┐
│  ◉ Ready         │  ← Status dot (green=connected, red=disconnected)
│                  │
│   [Jeff]       │  ← Device name
│                  │
│  🔋 87%  📶 ████ │  ← Battery + WiFi strength
│                  │
│  ↑↓ Select  ● OK │  ← Button hints
└──────────────────┘
```

### Voice Agent Mode
```
┌──────────────────┐
│  🎤 Listening... │
│                  │
│  "What's the     │
│   weather        │
│   today?"        │
│                  │
│  ─────────────── │
│  Thinking...     │
│                  │
│  ◉ 🔋 87%  📶 ██ │
└──────────────────┘
```

### Menu Screen
```
┌──────────────────┐
│  ▸ Voice Agent   │
│    Voice Note    │
│    Transcribe    │
│    Games         │
│    Dashboard     │
│    Settings      │
│                  │
│  ◉ 🔋 87%  📶 ██ │
└──────────────────┘
```

## Links

- [[Project Overview]]
- [[Hardware Details]]
- [[Hermes API Design]]
- [[UI Design]]
