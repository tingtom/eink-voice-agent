#pragma once
#include "esp_err.h"
esp_err_t mic_read(int16_t *buffer, size_t samples, size_t *read);
esp_err_t mic_raw_read(int16_t *buffer, size_t samples, size_t *bytes_read);
