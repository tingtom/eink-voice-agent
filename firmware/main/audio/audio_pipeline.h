// Audio pipeline
#pragma once
#include "esp_err.h"
typedef enum {
    MODE_AGENT,
    MODE_NOTE,
    MODE_TRANSCRIBE
} audio_mode_t;
void audio_pipeline_init(void);
void audio_pipeline_start_recording(audio_mode_t mode);
void audio_pipeline_stop_recording(void);
void audio_pipeline_play_tts(const uint8_t *audio, size_t len);
bool audio_pipeline_is_recording(void);
