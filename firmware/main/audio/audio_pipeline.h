#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

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

// Offline recording — writes audio to SPIFFS instead of WebSocket
bool audio_pipeline_start_offline_recording(void);
void audio_pipeline_stop_offline_recording(void);
bool audio_pipeline_is_offline_recording(void);
