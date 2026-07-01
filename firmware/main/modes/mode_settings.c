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
    char buf[128];
    switch (settings_selection) {
        case SETTINGS_WIFI_IP: {
            const char *ip = wifi_get_ip();
            if (ip && ip[0]) {
                snprintf(buf, sizeof(buf), "WiFi IP:\n%s", ip);
            } else {
                snprintf(buf, sizeof(buf), "WiFi IP:\nNot connected");
            }
            ui_show_response(buf);
            break;
        }
        case SETTINGS_BATTERY: {
            uint8_t pct = power_get_battery_pct();
            int mV = power_read_battery_mv();
            snprintf(buf, sizeof(buf), "Battery:\n%d%% (%.2fV)", pct, mV / 1000.0);
            ui_show_response(buf);
            break;
        }
        case SETTINGS_STORAGE: {
            uint64_t free_bytes = sdcard_get_free_bytes();
            uint64_t total_bytes = sdcard_get_total_bytes();
            size_t free_kb = free_bytes / 1024;
            size_t total_kb = total_bytes / 1024;
            snprintf(buf, sizeof(buf), "Storage:\n%dKB free / %dKB", free_kb, total_kb);
            ui_show_response(buf);
            break;
        }
        case SETTINGS_MANUAL_SYNC:
            if (!wifi_is_connected()) {
                ui_show_error("No WiFi");
                return;
            }
            ui_show_processing_screen();
            recording_sync_start();
            break;
    }
}

void mode_settings_finish(void)
{
    ESP_LOGI(TAG, "Settings mode finished");
}