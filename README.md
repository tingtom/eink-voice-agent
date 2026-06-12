# E-Ink Voice Agent

A handheld, battery-powered voice-first AI assistant built on the Waveshare ESP32-C6 e-Paper board. Always-on keyword detection for voice commands, with a small e-ink display for visual feedback. Connects to [Hermes Agent](https://hermes-agent.nousresearch.com) for LLM inference, transcription, note-taking, and games.

![Project Status](https://img.shields.io/badge/status-planning-yellow)
![Hardware](https://img.shields.io/badge/hardware-ESP32--C6-blue)
![Display](https://img.shields.io/badge/display-e--Paper%201.54%22%20200x200-black)

## Vision

A pocket-sized device you can talk to anywhere. Say the wake word, ask a question, get a response on the e-ink display (and optionally through a small speaker). Take voice notes that automatically sync to Obsidian. Play text trivia games. Check your Home Assistant dashboard — all from a device that runs for weeks on a single charge.

## Hardware

| Component | Model | Status |
|-----------|-------|--------|
| Main board | [Waveshare ESP32-C6-ePaper-1.54](https://www.waveshare.com/esp32-c6-epaper-1.54.htm) | 📦 Ordered |
| Microphone | INMP441 I2S MEMS | 🔜 To order |
| Speaker | MAX98357A I2S amplifier + small speaker | 🔜 To order |
| Battery | 3.7V LiPo 1000mAh+ | 🔜 To order |
| Charger | TP4056 USB-C module | 🔜 To order |
| Case | 3D printed PETG | 🔜 Design |

## Repository Structure

```
eink-voice-agent/
├── docs/                       # Project documentation
│   ├── project-overview.md
│   ├── hardware-details.md
│   ├── firmware-plan.md
│   ├── hermes-api-design.md
│   └── ui-design.md
├── firmware/                   # ESP-IDF firmware
│   ├── main/                   # Application code
│   ├── components/             # Third-party libraries
│   ├── models/                 # Edge Impulse TFLite models
│   ├── CMakeLists.txt
│   └── sdkconfig.defaults
├── hardware/                   # Hardware design files
│   ├── case/                   # 3D printable case (STL/STEP)
│   └── pcbs/                   # Custom PCB designs (if any)
├── scripts/                    # Build/flash/utilities
├── .opencode/                  # OpenCode project configuration
└── README.md
```

## Architecture

```
ESP32-C6 (e-Paper + Mic + Buttons)
       │ WiFi (WebSocket + HTTP)
       ▼
Hermes Agent (Home Server)
       ├── LLM inference (cloud)
       ├── Whisper transcription
       ├── TTS (edge-tts)
       ├── Obsidian vault (notes)
       ├── Home Assistant API
       └── Game logic
```

## Documentation

- [Project Overview](docs/project-overview.md) — Vision, use cases, architecture, BOM
- [Hardware Details](docs/hardware-details.md) — Board specs, pin mapping, power budget
- [Firmware Plan](docs/firmware-plan.md) — Code structure, task design, audio pipeline
- [Hermes API Design](docs/hermes-api-design.md) — WebSocket/HTTP protocol, messages
- [UI Design](docs/ui-design.md) — Screen layouts, fonts, refresh strategy

## Getting Started

### Prerequisites

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/index.html)
- [Edge Impulse CLI](https://docs.edgeimpulse.com/docs/tools/edge-impulse-cli) (for wake word training)
- [OpenCode](https://github.com/opencode-ai/opencode) (for AI-assisted firmware development)

### OpenCode

This project is configured for OpenCode. Start a coding session:

```bash
cd /root/eink-voice-agent
opencode
```

Use the TUI to describe what you want to build — OpenCode will read the docs, plan the implementation, and write the firmware code.

### Build & Flash

```bash
cd firmware
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Project Phases

1. **Hardware Bring-up** — Flash test firmware, verify display, test WiFi
2. **Audio Pipeline** — Microphone capture, wake word detection, audio streaming
3. **Display & UI** — E-ink layouts, button navigation, status bar
4. **Hermes Integration** — Voice agent, notes, transcription, TTS
5. **Games & Extras** — Trivia, word games, HA dashboard
6. **Hardware Integration** — 3D case, battery, final assembly

## License

MIT
