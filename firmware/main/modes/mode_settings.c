#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "ui_manager.h"
#include "wifi_manager.h"
#include "sdcard.h"
#include "power_mgmt.h"
#include "recordings.h"

static const char *TAG = "MODE_SETTINGS";
static int settings_selection = 0;

typedef enum {
    SETTINGS_WIFI_IP,
    SETTINGS_BATTERY,
    SETTINGS_STORAGE,
    SETTINGS_MANUAL_SYNC,
    SETTINGS_COUNT
} settings_item_t;

static const char *settings_labels[] = {
    "WiFi IP",
    "Battery",
    "Storage",
    "Manual Sync"
};

void mode_settings_start(void)
{
    ESP_LOGI(TAG, "Settings mode started");
    settings_selection = 0;
    ui_show_menu((const char **)settings_labels, SETTINGS_COUNT, settings_selection);
}

void mode_settings_next(void)
{
    settings_selection = (settings_selection + 1) % SETTINGS_COUNT;
    ui_show_menu((const char **)settings_labels, SETTINGS_COUNT, settings_selection);
}

void mode_settings_prev(void)
{
    settings_selection = (settings_selection > 0) ? settings_selection - 1 : SETTINGS_COUNT - 1;
    ui_show_menu((const char **)settings_labels, SETTINGS_COUNT, settings_selection);
}

void mode_settings_select(void)
{
    switch (settings_selection) {
        case SETTINGS_MANUAL_SYNC:
            if (!wifi_is_connected()) {
                ui_show_error("No WiFi");
                return;
            }
            ui_show_processing_screen();
            recording_sync_start();
            break;
        default:
            return;
    }
}

void mode_settings_finish(void)
{
    ESP_LOGI(TAG, "Settings mode finished");
}