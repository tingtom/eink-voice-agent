#pragma once
#include "esp_err.h"
#include "esp_codec_dev.h"

esp_err_t es8311_prealloc_i2s(void);
esp_err_t es8311_init(void);
void es8311_set_volume(uint8_t vol);
void es8311_deinit(void);
esp_codec_dev_handle_t es8311_get_playback_handle(void);
esp_codec_dev_handle_t es8311_get_record_handle(void);
