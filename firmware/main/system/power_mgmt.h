#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

void power_init(void);
uint8_t power_get_battery_pct(void);
bool power_is_charging(void);
bool power_should_sleep(void);
void power_mark_activity(void);
void power_enter_deep_sleep(uint64_t wake_time_us);
