// Mode: Dashboard
#pragma once
#include <stdbool.h>
void mode_dashboard_start(void);
void mode_dashboard_refresh(void);
void mode_dashboard_set_data(const char *weather, const char *next_event, int lights_on, bool doors_locked);
