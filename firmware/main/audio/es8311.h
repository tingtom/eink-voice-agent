#pragma once
#include "esp_err.h"

esp_err_t es8311_init(void);
void es8311_set_volume(uint8_t vol);
