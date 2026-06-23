# Plan: SD Card Audio Storage + Recording Manager

## Goal
Replace 512KB SPIFFS offline audio storage with microSD card, adding a proper recording data model (type/status), capacity display, and updated UI.

---

## Wave 1 — Partition & SD Card Foundation

### T1.1 Update partition table
**File**: `firmware/partitions.csv`
- Board has 16MB flash (not 2MB as currently partitioned)
- Remove SPIFFS `storage` partition entirely — audio goes to SD now
- Adjust `factory` partition to use remaining space
- Keep `nvs` (16KB) and `phy_init` (4KB)
- All small data (Tomagotchi state, settings) stays in NVS

### T1.2 Create SD card driver
**Files**: `firmware/main/storage/sdcard.c`, `firmware/main/storage/sdcard.h`
- Wrap `esp_vfs_fat_sdspi_mount()` using Waveshare's proven pattern
- SPI2 shared bus: MISO=GPIO4, MOSI=GPIO5, CLK=GPIO6, CS=GPIO3
- Must NOT re-init the SPI bus if e-paper already initialized it (use flag/probe)
- Provide `sdcard_mount()`, `sdcard_unmount()`, `sdcard_get_free_bytes()`
- Mount point: `/sdcard`

### T1.3 Create recording manager
**Files**: `firmware/main/storage/recordings.c`, `firmware/main/storage/recordings.h`
- New data model replacing old `offline_notes.c/h`:
  ```c
  typedef enum {
      REC_TYPE_NOTE = 0,
      REC_TYPE_TODO = 1
  } rec_type_t;

  typedef enum {
      REC_STATUS_RAW = 0,
      REC_STATUS_TRANSCRIBED = 1,
      REC_STATUS_SYNCED = 2
  } rec_status_t;
  ```
- Recording directory: `/sdcard/recordings/`
- `.meta` binary format: `[ts:u32][duration_ms:u32][type:u8][status:u8][text_len:u16][text...]`
- Functions: `init`, `start_recording`, `write_audio`, `stop_recording`, `list`, `get_status`, `get_capacity_seconds`, `get_avg_recording_len`, `play`, `sync_start`, `sync_handle_response`, `delete`

### T1.4 Remove old files
**Files**: `firmware/main/storage/offline_notes.c`, `firmware/main/storage/offline_notes.h`
- Delete both files

---

## Wave 2 — Core Integration

### T2.1 Update audio pipeline
**File**: `firmware/main/audio/audio_pipeline.c`, `firmware/main/audio/audio_pipeline.h`
- Replace `offline_note_write_audio()` calls with `recording_write_audio()`
- Same architecture — ringbuffer → write function, just different destination

### T2.2 Update speaker driver for SD playback
**File**: `firmware/main/audio/speaker_driver.c`, `firmware/main/audio/speaker_driver.h`
- `speaker_play_file()` already reads via `fopen` — just update the path prefix from `/spiffs/` to `/sdcard/`
- No structural changes needed

### T2.3 Update mode_note
**File**: `firmware/main/modes/mode_note.c`
- Use new `recordings.h` API instead of `offline_notes.h`
- Online: stream via WebSocket (unchanged)
- Offline: `recording_start(REC_TYPE_NOTE)` → SD card
- Display capacity estimate before recording: "~XXm YYs available"
- Tick down remaining time during recording

### T2.4 Update mode_todo
**File**: `firmware/main/modes/mode_todo.c`
- Use `recording_start(REC_TYPE_TODO)` when offline
- Online: stream via WebSocket (unchanged)

### T2.5 Update main.c — UI + Sync
**File**: `firmware/main/main.c`
- Replace all `offline_notes_*` calls with `recordings_*`
- **View Notes screen**: Show list with status icons:
  - RAW: `◉ name` (unsynced dot) → press to listen, long-press to delete
  - TRANSCRIBED: `T name · "text preview..."` → press to view full text, long-press to delete
  - SYNCED: `✓ name · "text..."` → press to view, long-press to delete
- **Sync flow**: `recording_sync_start()` sends unsynced RAW/TRANSCRIBED files via WebSocket
- **Capacity display**: On record-ready screen, show "~XXm YYs recording space"
- Init order: mount SD card → mount recordings → mount NVS → WiFi → WebSocket

### T2.6 Update CMakeLists.txt
**File**: `firmware/main/CMakeLists.txt`
- Remove `storage/offline_notes.c` from SRCS
- Remove `spiffs` from REQUIRES
- Add `storage/sdcard.c`, `storage/recordings.c` to SRCS
- Add `fatfs` to REQUIRES
- Verify no stale references

---

## Wave FINAL — Verification

| # | Check |
|---|-------|
| V1 | `lsp_diagnostics` clean on all changed C files |
| V2 | `idf.py build` exits 0 with 0 warnings |
| V3 | Verify partition table is valid (check_sizes.py passes) |
| V4 | Review: old SPIFFS `offline_notes` imports completely gone |
| V5 | Review: all `NOTES_DIR "/spiffs"` references replaced with SD paths |
| V6 | Review: e-paper still works (SD shares same SPI bus — verify CS arbitration) |

---

## Dependencies & Ordering

```
T1.1 (partitions) ── independent
T1.2 (sdcard) ───── independent
T1.3 (recordings) ── depends on T1.2
T1.4 (delete old) ── independent
        │
        ▼
T2.1 (audio_pipeline) ── depends on T1.3
T2.2 (speaker) ───────── independent
T2.3 (mode_note) ─────── depends on T1.3
T2.4 (mode_todo) ─────── depends on T1.3
T2.5 (main.c) ────────── depends on T1.3, T2.3, T2.4
T2.6 (cmake) ─────────── depends on all
        │
        ▼
V1-V6 ────────────────── after all T2 complete
```

**Parallel execution within each wave where dependencies allow.**
