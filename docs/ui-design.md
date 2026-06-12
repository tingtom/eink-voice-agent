# UI Design — E-Ink Voice Agent

## Display Constraints

- **Resolution:** 200×200 pixels
- **Colors:** Black/white (no grayscale on some models — confirm with Waveshare)
- **Refresh:** Full refresh ~2s, partial refresh ~0.3s
- **No touch** — all input via buttons

## Design Principles

1. **Large, readable text** — minimum 12px font at 200×200
2. **Minimal elements per screen** — avoid clutter
3. **High contrast** — black on white, no anti-aliasing tricks
4. **Partial refresh for responsiveness** — only full refresh when needed
5. **Status bar always visible** — battery, WiFi, mode indicator

## Font Sizes

| Font | Size | Use |
|------|------|-----|
| Small | 8×12 px | Status bar, hints, labels |
| Medium | 12×16 px | Menu items, body text |
| Large | 16×24 px | Headings, prompts |
| XL | 24×32 px | Large status icons |

## Screen Layouts

### 1. Boot Screen
```
┌────────────────────┐
│                    │
│    ◉ Merlin        │
│                    │
│   Connecting...    │
│                    │
│   ████████░░ 80%   │
│                    │
└────────────────────┘
```

### 2. Idle / Home
```
┌────────────────────┐
│ ◉ Ready      10:30 │
│                    │
│                    │
│   🎤 Say "Hey      │
│      Merlin"       │
│                    │
│                    │
│ 🔋87%  📶████  22° │
└────────────────────┘
```

### 3. Voice Recording
```
┌────────────────────┐
│ 🎤 Listening...    │
│ ═══════════════    │
│ ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ │  ← Audio level meter
│                    │
│ "What's the        │
│  weather?"         │
│                    │
│ 🔋87%  📶████      │
└────────────────────┘
```

### 4. Processing
```
┌────────────────────┐
│ ◉ Thinking...      │
│                    │
│  ○ ○ ○             │  ← Animated dots
│                    │
│                    │
│                    │
│ 🔋87%  📶████      │
└────────────────────┘
```

### 5. Response Display
```
┌────────────────────┐
│ ◉ Response         │
│                    │
│ 22°C, sunny.       │
│ Light breeze.      │
│ UV index: 4        │
│                    │
│ 🔋87%  📶████      │
└────────────────────┘
```

### 6. Menu
```
┌────────────────────┐
│ ▸ Voice Agent   ▲  │
│   Voice Note    ▼  │
│   Transcribe       │
│   Games            │
│   Dashboard        │
│   Settings         │
│ 🔋87%  📶████      │
└────────────────────┘
```

### 7. Games Menu
```
┌────────────────────┐
│ ▸ Trivia Quiz   ▲  │
│   Word Game     ▼  │
│   20 Questions     │
│   Math Challenge   │
│                    │
│                    │
│ 🔋87%  📶████      │
└────────────────────┘
```

### 8. Dashboard
```
┌────────────────────┐
│ ◉ Dashboard        │
│                    │
│ 🌡️ 22°C  ☀️ Sunny  │
│ 🚪 Locked  💡 3 on │
│                    │
│ Next: Standup 11:00│
│                    │
│ 🔋87%  📶████      │
└────────────────────┘
```

### 9. Settings
```
┌────────────────────┐
│ ▢ Wake Word     ▲  │
│   Sensitivity: 70% │
│   TTS Voice: Sonia │
│   Sleep: 5 min  ▼  │
│   Brightness: 100% │
│                    │
│ 🔋87%  📶████      │
└────────────────────┘
```

### 10. Note Taking
```
┌────────────────────┐
│ 📝 Recording Note  │
│ ═══════════════    │
│ ▓▓▓▓▓▓▓░░░░░░░░░ │
│                    │
│ 00:42              │
│                    │
│ [●] Save  [✕] Disc │
│ 🔋87%  📶████      │
└────────────────────┘
```

### 11. Transcription
```
┌────────────────────┐
│ ◉ Transcribing...  │
│                    │
│ "Meeting notes     │
│  from today's      │
│  standup. We       │
│  discussed..."     │
│                    │
│ 🔋87%  📶████      │
└────────────────────┘
```

### 12. Error
```
┌────────────────────┐
│ ⚠️ Error           │
│                    │
│ Cannot connect     │
│ to Hermes server   │
│                    │
│ Retrying in 30s... │
│                    │
│ 🔋87%  📶░░░░      │
└────────────────────┘
```

## Button Mapping

| Screen | UP | DOWN | SELECT | BACK |
|--------|----|------|--------|------|
| Idle | — | — | Open menu | — |
| Menu | Prev item | Next item | Enter mode | Close menu |
| Voice | — | — | Cancel | Return to menu |
| Response | Scroll up | Scroll down | New query | Return to menu |
| Games | — | — | Start game | Return to menu |
| Settings | Prev setting | Next setting | Adjust value | Save & exit |

## E-Ink Refresh Strategy

| Action | Refresh Type | Duration |
|--------|-------------|----------|
| Boot screen | Full | ~2s |
| Status bar update | Partial | ~0.3s |
| Menu scroll | Partial | ~0.3s |
| New screen | Full | ~2s |
| Audio level meter | Partial | ~0.1s |
| Text update | Partial | ~0.3s |

## Links

- [[Project Overview]]
- [[Firmware Plan]]
- [[Hermes API Design]]
