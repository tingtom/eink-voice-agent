#pragma once
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"

esp_err_t es8311_init(i2s_chan_handle_t tx_handle);
void es8311_set_volume(uint8_t vol);
void es8311_deinit(void);
esp_codec_dev_handle_t es8311_get_handle(void);

