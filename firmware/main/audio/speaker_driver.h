#pragma once
#include "esp_err.h"
#include <stddef.h>
esp_err_t speaker_play(const int16_t *audio, size_t samples);
void speaker_set_volume(uint8_t vol);
void speaker_play_file(const char *pcm_path);
void speaker_stop(void);
esp_err_t speaker_play_tone(int freq_hz, int duration_ms);
