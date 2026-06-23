#pragma once
#include <stdint.h>
#include "esp_err.h"
void system_init(void);
void *get_i2c_bus_handle(void);
uint8_t tca9554_read_input(void);
