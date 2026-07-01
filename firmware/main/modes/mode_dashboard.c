#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "mode_dashboard.h"
#include "esp_log.h"
#include "ui_manager.h"

static const char *TAG = "MODE_DASHBOARD";
static bool active = false;

void mode_dashboard_start(void)
{
    ESP_LOGI(TAG, "Dashboard mode started");
    active = true;
    ui_show_response("Dashboard\nLoading...");
}

void mode_dashboard_refresh(void)
{
    if (!active) return;
    ui_show_processing_screen("Loading...");
}

void mode_dashboard_set_data(const char *weather, const char *next_event, int lights_on, bool doors_locked)
{
    if (!active) return;

    char buf[128];
    snprintf(buf, sizeof(buf),
             "%s\n%s\nLights: %d  Door: %s",
             weather ? weather : "N/A",
             next_event ? next_event : "No events",
             lights_on,
             doors_locked ? "Locked" : "Unlocked");
    ui_show_response(buf);
}
