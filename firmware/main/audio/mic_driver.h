// Microphone driver (I2S)
#pragma once
#include "esp_err.h"
void mic_init(void);
esp_err_t mic_read(int16_t *buffer, size_t samples, size_t *read);
void mic_start(void);
void mic_stop(void);
