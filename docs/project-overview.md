# E-Ink Voice Agent — Project Overview

**Status:** 🟡 Planning
**Started:** 2026-06-12
**Hardware:** Waveshare ESP32-C6 1.54" e-Paper (200×200)
**Repo:** [github.com/tom-hermes/eink-voice-agent](https://github.com/tom-hermes/eink-voice-agent) *(to create)*

## Vision

A handheld, battery-powered voice-first AI assistant built on the Waveshare ESP32-C6 e-Paper board. Always-on keyword detection for voice commands, with a small e-ink display for visual feedback. Connects to Hermes Agent for heavy lifting (LLM, transcription, note-taking, games).

## Core Use Cases

1. **Voice Agent** — Talk to Hermes through the device. Wake word → record → stream to Hermes → display response on e-ink.
2. **Note Taking** → Voice memo → transcribe via Hermes → save to Obsidian.
3. **Transcription** — Record audio on-device, send to Hermes for Whisper transcription.
4. **Games** — Simple text-based or trivia games rendered on e-ink, powered by Hermes.
5. **Quick Reference** — Display calendar, weather, HA status, todo list on e-ink.

## Architecture

```
┌─────────────────────────────────────────────┐
│  ESP32-C6 (Waveshare e-Paper 1.54")         │
│  ┌──────────┐  ┌──────────┐  ┌───────────┐ │
│  │ INMP441  │  │ E-Ink    │  │ Buttons   │ │
│  │ I2S Mic  │  │ 200×200  │  │ (3-4x)    │ │
│  └────┬─────┘  └────▲─────┘  └─────▲─────┘ │
│       │              │              │       │
│  ┌────▼──────────────┴──────────────┴────┐  │
│  │  Firmware (ESP-IDF / Arduino)         │  │
│  │  - Wake word detection (Edge Impulse) │  │
│  │  - Audio capture & streaming          │  │
│  │  - E-ink display driver               │  │
│  │  - WiFi management                    │  │
│  │  - HTTP/WebSocket client              │  │
│  │  - Button input handling              │  │
│  └────────────────┬──────────────────────┘  │
│                   │ WiFi                     │
└───────────────────┼─────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────┐
│  Hermes Agent (Home Server)                  │
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐│
│  │ LLM      │ │ Whisper  │ │ Obsidian     ││
│  │ (cloud)  │ │ (local)  │ │ (vault)      ││
│  └──────────┘ └──────────┘ └──────────────┘│
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐│
│  │ TTS      │ │ HA API   │ │ Game Logic   ││
│  │ (edge)   │ │          │ │              ││
│  └──────────┘ └──────────┘ └──────────────┘│
└─────────────────────────────────────────────┘
```

## Hardware Bill of Materials

| Component | Model | Est. Cost | Status |
|-----------|-------|-----------|--------|
| Main board | Waveshare ESP32-C6-ePaper-1.54 | ~£25 | 📦 Ordered |
| Microphone | INMP441 I2S MEMS | ~£3 | 🔜 To order |
| Speaker | Small I2S speaker (MAX98357A) | ~£5 | 🔜 To order |
| Battery | 3.7V LiPo 1000mAh+ | ~£8 | 🔜 To order |
| Charger | TP4056 USB-C module | ~£2 | 🔜 To order |
| Buttons | Tactile switches (×4) | ~£1 | 🔜 To order |
| Case | 3D printed (PETG) | ~£5 | 🔜 Design |
| Misc | Wires, headers, proto board | ~£5 | 🔜 To order |

**Estimated total:** ~£55

## Key Technical Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Firmware framework | ESP-IDF | Better I2S/audio support than Arduino |
| Wake word | Edge Impulse (TFLite) | Offline, low-power, customizable |
| Audio streaming | WebSocket to Hermes | Low latency, bidirectional |
| Display partial refresh | Waveshare SDK | Fast updates without full flash |
| Power management | ESP32-C6 deep sleep | Wake on button or voice activity |
| Case | 3D printed PETG | Custom fit, durable |

## Project Phases

### Phase 1: Hardware Bring-up 🔜
- [ ] Receive Waveshare board, flash test firmware
- [ ] Verify e-ink display works (demo waveshare app)
- [ ] Set up ESP-IDF build environment
- [ ] Test WiFi connectivity

### Phase 2: Audio Pipeline 🔜
- [ ] Wire INMP441 microphone to ESP32-C6 I2S
- [ ] Capture raw audio, verify signal
- [ ] Train wake word model (Edge Impulse)
- [ ] Stream audio to Hermes via WebSocket

### Phase 3: Display & UI 🔜
- [ ] Design e-ink UI layouts (text, menus, status)
- [ ] Implement partial refresh for responsive feel
- [ ] Button navigation (up/down/select/back)
- [ ] Status bar (WiFi, battery, time)

### Phase 4: Hermes Integration 🔜
- [ ] Build Hermes API endpoints for device
- [ ] Voice → Hermes → LLM → display response
- [ ] Note capture → Hermes → Obsidian
- [ ] TTS response playback through speaker

### Phase 5: Games & Extras 🔜
- [ ] Text adventure game engine
- [ ] Trivia / quiz games
- [ ] HA dashboard view
- [ ] Calendar / agenda display

### Phase 6: Hardware Integration 🔜
- [ ] Design 3D printed case
- [ ] Battery + charging circuit
- [ ] Final assembly
- [ ] Power optimization & battery life testing

## Links

- [[Hardware Details]]
- [[Firmware Plan]]
- [[Hermes API Design]]
- [[UI Design]]
- [[3D Case Design]]
- [[Bill of Materials]]
- [[Timeline]]
