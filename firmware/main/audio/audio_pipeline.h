#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "recordings.h"

typedef enum {
    MODE_AGENT,
    MODE_NOTE,
    MODE_TRANSCRIBE,
    MODE_TODO
} audio_mode_t;

void audio_pipeline_init(void);
void audio_pipeline_start_recording(audio_mode_t mode);
void audio_pipeline_stop_recording(void);
void audio_pipeline_start_processing(void);
void audio_pipeline_stop_processing(void);
void audio_pipeline_send_end_recording(void);
void audio_pipeline_play_tts(const uint8_t *audio, size_t len);
audio_mode_t audio_pipeline_get_current_mode(void);
bool audio_pipeline_is_recording(void);

// Offline recording — writes audio to SD card
bool audio_pipeline_start_offline_recording(rec_type_t type);
void audio_pipeline_stop_offline_recording(void);
bool audio_pipeline_is_offline_recording(void);

// Dock control — when docked, VAD and wake word are suppressed
void audio_pipeline_set_docked(bool docked);
bool audio_pipeline_is_docked(void);

// Playback control — suppress mic/VAD during speaker output
void audio_pipeline_set_playback_active(bool active);

// UI state callbacks
typedef void (*audio_ui_cb_t)(void);
typedef void (*response_cb_t)(const char *text);
void audio_pipeline_set_wake_failed_cb(audio_ui_cb_t cb);
void audio_pipeline_set_wake_detected_cb(audio_ui_cb_t cb);
void audio_pipeline_set_recording_ended_cb(audio_ui_cb_t cb);
void audio_pipeline_set_response_cb(response_cb_t cb);
