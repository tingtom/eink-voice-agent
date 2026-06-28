#pragma once
#include "esp_err.h"
#include <stddef.h>
void speaker_set_handle(void *handle);
void speaker_init(void);
esp_err_t speaker_enable(void);
esp_err_t speaker_play(const int16_t *audio, size_t samples);
void speaker_play_file(const char *pcm_path);
void speaker_stop(void);
void speaker_set_volume(uint8_t vol);
esp_err_t speaker_play_tone(int freq_hz, int duration_ms);
