# Hermes API Design — E-Ink Voice Agent

## Overview

The ESP32-C6 device communicates with Hermes Agent running on the home server. Hermes handles all heavy processing: LLM inference, transcription, TTS, Obsidian integration, and game logic.

## Communication Protocol

**Primary:** WebSocket for real-time bidirectional audio streaming
**Secondary:** HTTP REST for configuration, status, and non-real-time operations

## WebSocket Endpoint

### `ws://hermes-server:8123/api/device/ws`

Bidirectional streaming connection. Device sends audio chunks, Hermes sends responses.

#### Device → Hermes Messages

```json
// Authentication
{
  "type": "auth",
  "device_id": "eink-agent-001",
  "token": "device-api-token"
}

// Start voice session
{
  "type": "voice_start",
  "session_id": "uuid-v4",
  "mode": "agent"  // "agent", "note", "transcribe"
}

// Audio chunk (base64 encoded PCM)
{
  "type": "audio",
  "session_id": "uuid-v4",
  "data": "base64-encoded-pcm-16bit-16khz-mono",
  "seq": 42
}

// End of audio (user stopped speaking)
{
  "type": "audio_end",
  "session_id": "uuid-v4"
}

// Button event
{
  "type": "button",
  "button": "select",  // "up", "down", "select", "back"
  "session_id": "uuid-v4"
}

// Ping (keepalive)
{
  "type": "ping",
  "timestamp": 1718200000
}
```

#### Hermes → Device Messages

```json
// Auth response
{
  "type": "auth_ok",
  "device_id": "eink-agent-001"
}

// Status update (display on e-ink)
{
  "type": "status",
  "text": "Listening...",
  "icon": "mic"  // "mic", "thinking", "speaking", "error", "ok"
}

// Display text (for e-ink screen)
{
  "type": "display",
  "lines": [
    "What's the weather",
    "today?",
    "",
    "Thinking..."
  ],
  "mode": "replace"  // "replace", "append", "scroll"
}

// LLM response text
{
  "type": "response",
  "text": "It's 22°C and sunny in London today. Light breeze from the west.",
  "session_id": "uuid-v4"
}

// TTS audio (base64 encoded, device plays through speaker)
{
  "type": "tts",
  "data": "base64-encoded-pcm-audio",
  "format": "pcm16",
  "sample_rate": 16000
}

// Menu navigation
{
  "type": "menu",
  "items": [
    {"id": "voice", "label": "Voice Agent", "icon": "mic"},
    {"id": "note", "label": "Voice Note", "icon": "note"},
    {"id": "transcribe", "label": "Transcribe", "icon": "text"},
    {"id": "games", "label": "Games", "icon": "game"},
    {"id": "dashboard", "label": "Dashboard", "icon": "home"}
  ],
  "selected": 0
}

// Note saved confirmation
{
  "type": "note_saved",
  "title": "Voice Note - 2026-06-12 10:30",
  "obsidian_path": "Daily/2026-06-12.md"
}

// Transcription result
{
  "type": "transcription",
  "text": "Meeting notes from today's standup...",
  "language": "en"
}

// Error
{
  "type": "error",
  "code": "connection_failed",
  "message": "Could not reach Hermes server"
}

// Pong (keepalive response)
{
  "type": "pong",
  "timestamp": 1718200000
}
```

## HTTP REST Endpoints

### Device Registration

```
POST /api/device/register
{
  "device_id": "eink-agent-001",
  "name": "Living Room Agent",
  "capabilities": ["voice", "display", "buttons"]
}
→ { "token": "device-api-token", "ws_url": "ws://..." }
```

### Status / Health

```
GET /api/device/status
Authorization: Bearer device-api-token
→ {
  "connected": true,
  "battery_pct": 87,
  "wifi_rssi": -45,
  "uptime_seconds": 3600,
  "last_seen": "2026-06-12T10:30:00Z"
}
```

### Configuration

```
GET /api/device/config
→ {
  "wake_word": "hi jeff",
  "wake_word_sensitivity": 0.7,
  "tts_voice": "edge-tts-en-GB-Sonia",
  "display_brightness": 100,
  "sleep_timeout_seconds": 300,
  "hermes_llm_model": "openrouter/owl-alpha"
}

PUT /api/device/config
{ "wake_word_sensitivity": 0.8 }
→ { "ok": true }
```

### Obsidian Integration

```
POST /api/device/note
{
  "title": "Voice Note - 2026-06-12 10:30",
  "content": "Transcribed voice note content...",
  "folder": "Daily",
  "tags": ["voice-note"]
}
→ { "path": "Daily/2026-06-12.md", "url": "obsidian://..." }
```

### Home Assistant Dashboard Data

```
GET /api/device/dashboard
→ {
  "weather": { "temp": 22, "condition": "sunny" },
  "next_calendar_event": "Team standup at 11:00",
  "lights_on": 3,
  "doors_locked": true
}
```

## Audio Format

| Parameter | Value |
|-----------|-------|
| Codec | PCM 16-bit signed |
| Sample rate | 16000 Hz |
| Channels | 1 (mono) |
| Chunk size | 4096 samples (256ms) |
| Encoding over wire | Base64 |

## TTS Audio Format

| Parameter | Value |
|-----------|-------|
| Codec | PCM 16-bit signed |
| Sample rate | 16000 Hz (or 24000 Hz) |
| Channels | 1 (mono) |
| Delivery | WebSocket message, base64 encoded |

## Security

- Device authenticates with token on WebSocket connect
- Token issued during device registration
- All communication over WSS (TLS) in production
- Rate limiting: max 60 voice sessions per hour per device
- Audio data not persisted on server (streaming only)

## Implementation Notes

- WebSocket server can be a Hermes skill or standalone service
- Audio streaming uses chunked base64 to avoid binary WebSocket complexity on ESP32
- Device should auto-reconnect WebSocket with exponential backoff
- Hermes processes audio through: VAD → Whisper → LLM → TTS pipeline
- For "note" mode: skip LLM, go straight to Whisper → Obsidian
- For "transcribe" mode: skip LLM and TTS, return text only

## Links

- [[Project Overview]]
- [[Firmware Plan]]
- [[UI Design]]
