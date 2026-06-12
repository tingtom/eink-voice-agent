#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_sleep.h"

void power_init(void);
uint8_t power_get_battery_pct(void);
bool power_is_charging(void);
bool power_should_sleep(void);
void power_mark_activity(void);
esp_err_t power_ulp_load(void);
esp_sleep_source_t power_enter_deep_sleep(void);
void power_enter_timer_sleep(uint32_t duration_ms);
