// Speaker driver (I2S → MAX98357A)
#pragma once
#include "esp_err.h"
void speaker_init(void);
esp_err_t speaker_play(const int16_t *audio, size_t samples);
void speaker_stop(void);
void speaker_set_volume(uint8_t vol);  // 0-100
