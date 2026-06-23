#pragma once
#include "esp_err.h"
#include <stddef.h>
#include "driver/i2s_std.h"
void speaker_set_handle(i2s_chan_handle_t handle);
void speaker_init(void);
esp_err_t speaker_play(const int16_t *audio, size_t samples);
void speaker_play_file(const char *pcm_path);
void speaker_stop(void);
void speaker_set_volume(uint8_t vol);  // 0-100
