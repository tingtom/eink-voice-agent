#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "app_config.h"
#include "epaper_driver.h"

static const char *TAG = "UI";
static char status_text[32] = {0};
static bool wifi_ok = false;
static uint8_t battery = 0;

static void draw_status_bar(void)
{
    char line[64];
    snprintf(line, sizeof(line), "%c %s  %s",
             wifi_ok ? 'O' : 'X',
             status_text,
             "88%");
    epaper_draw_text(0, 0, line, 8);

    char buf[16];
    snprintf(buf, sizeof(buf), "BAT:%d%%", battery);
    epaper_draw_text(DISPLAY_WIDTH - 60, 0, buf, 8);
}

void ui_init(void)
{
    epaper_clear();
    ESP_LOGI(TAG, "UI initialized");
}

void ui_show_boot_screen(const char *device_name)
{
    epaper_clear();
    epaper_draw_text(DISPLAY_WIDTH / 2 - 20, DISPLAY_HEIGHT / 2 - 12, device_name, 16);
    epaper_draw_text(DISPLAY_WIDTH / 2 - 30, DISPLAY_HEIGHT / 2 + 12, "Connecting...", 8);
    epaper_full_refresh();
}

void ui_show_home_screen(void)
{
    epaper_clear();
    epaper_draw_text(10, 60, "Say 'Hey Merlin'", 12);
    epaper_draw_text(10, 80, "or press SELECT", 12);
    draw_status_bar();
    epaper_full_refresh();
}

void ui_show_menu(const char **items, int count, int selected)
{
    epaper_clear();
    for (int i = 0; i < count; i++) {
        char line[32];
        if (i == selected) {
            snprintf(line, sizeof(line), "> %s", items[i]);
        } else {
            snprintf(line, sizeof(line), "  %s", items[i]);
        }
        epaper_draw_text(10, 20 + i * 20, line, 12);
    }
    draw_status_bar();
    epaper_partial_refresh();
}

void ui_show_recording_screen(void)
{
    epaper_clear();
    epaper_draw_text(10, 40, "Listening...", 16);
    epaper_draw_text(10, 80, "Press BACK to cancel", 8);
    draw_status_bar();
    epaper_partial_refresh();
}

void ui_show_processing_screen(void)
{
    epaper_clear();
    epaper_draw_text(10, 60, "Thinking...", 16);
    draw_status_bar();
    epaper_partial_refresh();
}

void ui_show_response(const char *text)
{
    epaper_clear();
    epaper_draw_text(10, 20, text, 12);
    epaper_draw_text(10, DISPLAY_HEIGHT - 20, "SELECT=back", 8);
    draw_status_bar();
    epaper_partial_refresh();
}

void ui_show_error(const char *message)
{
    epaper_clear();
    epaper_draw_text(10, 60, "Error:", 16);
    epaper_draw_text(10, 80, message, 12);
    epaper_full_refresh();
}

void ui_show_sleep_screen(void)
{
    epaper_clear();
    epaper_draw_text(DISPLAY_WIDTH / 2 - 30, DISPLAY_HEIGHT / 2, "Sleeping...", 12);
    epaper_full_refresh();
}

void ui_update_status_bar(bool connected, uint8_t battery_pct)
{
    wifi_ok = connected;
    battery = battery_pct;
}

void ui_update_battery(uint8_t pct)
{
    battery = pct;
}

void ui_update_wifi_status(bool connected)
{
    wifi_ok = connected;
}

void ui_set_status_text(const char *text)
{
    strncpy(status_text, text, sizeof(status_text) - 1);
    status_text[sizeof(status_text) - 1] = '\0';
}
