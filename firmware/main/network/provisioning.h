#pragma once
#include "esp_err.h"

esp_err_t provisioning_start_ap(void);
esp_err_t provisioning_start_server(void);
void provisioning_stop(void);
bool provisioning_is_active(void);
