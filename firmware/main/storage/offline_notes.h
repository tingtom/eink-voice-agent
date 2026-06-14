#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MAX_NOTES           50
#define NOTE_NAME_LEN       16
#define NOTE_TEXT_MAX       256
#define NOTES_DIR           "/spiffs"
#define PCM_CHUNK_SAMPLES   1024

void offline_notes_init(void);

// Recording
bool offline_note_start(void);
void offline_note_write_audio(const int16_t *data, size_t samples);
void offline_note_stop(void);
bool offline_note_is_recording(void);

// Listing
int  offline_note_count(void);
int  offline_note_pending_sync_count(void);
bool offline_note_get_name(int idx, char *name, size_t name_sz);
bool offline_note_get_text(int idx, char *text, size_t text_sz);
bool offline_note_is_synced(int idx);
uint32_t offline_note_get_timestamp(int idx);

// Playback
void offline_note_play(int idx);

// Sync
void offline_note_sync_start(void);
bool offline_note_sync_is_busy(void);

// Delete
void offline_note_delete(int idx);
