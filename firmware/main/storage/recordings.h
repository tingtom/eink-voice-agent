#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define REC_MAX_NOTES       20
#define REC_NAME_LEN        16
#define REC_TEXT_MAX        128

#define REC_DIR             "/sdcard/recordings"
#define PCM_CHUNK_SAMPLES   1024
#define PCM_BYTES_PER_SEC   32000  // 16kHz × 16-bit × 1ch

typedef enum {
    REC_TYPE_NOTE = 0,
    REC_TYPE_TODO = 1,
    REC_TYPE_AGENT = 2,
    REC_TYPE_TRANSCRIBE = 3
} rec_type_t;

typedef enum {
    REC_STATUS_RAW = 0,         // recorded offline, not yet sent
    REC_STATUS_TRANSCRIBED = 1, // text received from Hermes
    REC_STATUS_SYNCED = 2       // fully processed (todo pushed, etc.)
} rec_status_t;

typedef struct {
    char        name[REC_NAME_LEN]; // "rec_00001"
    rec_type_t  type;
    rec_status_t status;
    uint32_t    timestamp;      // unix time (esp_timer epoch)
    uint32_t    duration_ms;    // recording duration
    char        text[REC_TEXT_MAX]; // transcribed text
} recording_info_t;

/**
 * @brief Init SD card and scan existing recordings.
 * Must be called once at startup.
 */
bool recordings_init(void);
void recordings_init_audio(void);

// ── Recording ───────────────────────────────────────

bool recording_start(rec_type_t type);
void recording_write_audio(const int16_t *data, size_t samples);
void recording_stop(void);
bool recording_is_active(void);

// ── Listing ─────────────────────────────────────────

int  recording_count(void);
bool recording_get_info(int idx, recording_info_t *info);
int  recording_pending_sync_count(void);

// ── Status updates ──────────────────────────────────

void recording_set_text(int idx, const char *text);
void recording_set_synced(int idx);

// ── Capacity ────────────────────────────────────────

/**
 * @brief Estimated recording seconds remaining on SD card.
 * Based on free space / 32000 bytes per second.
 */
uint32_t recording_capacity_seconds(void);

/**
 * @brief Average duration of existing recordings (seconds).
 * Returns 10 if no recordings exist yet.
 */
uint32_t recording_avg_duration_seconds(void);

// ── Playback ────────────────────────────────────────

void recording_play(int idx);

// ── Sync ────────────────────────────────────────────

void recording_sync_start(void);
bool recording_sync_is_busy(void);
void recording_sync_handle_response(const char *session, const char *text);

// ── Delete ──────────────────────────────────────────

void recording_delete(int idx);
