#pragma once
#include "esp_err.h"
#include "driver/i2s_std.h"
void mic_set_handle(i2s_chan_handle_t handle);
void mic_init(void);
esp_err_t mic_read(int16_t *buffer, size_t samples, size_t *read);
void mic_start(void);
void mic_stop(void);
