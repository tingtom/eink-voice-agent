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

static void draw_wifi_icon(int x, int y, bool connected)
{
    int bars[3] = {connected ? 9 : 9, connected ? 7 : 9, connected ? 5 : 9};
    for (int i = 0; i < 3; i++) {
        int x1 = x + (9 - bars[i]) / 2;
        epaper_draw_line(x1, y + i * 3, x1 + bars[i] - 1, y + i * 3);
    }
}

static void draw_battery_icon(int x, int y, int pct)
{
    int bw = 18, bh = 8;
    epaper_draw_rect(x, y, bw, bh, 0);
    epaper_draw_rect(x + bw, y + 2, 2, bh - 4, 1);
    int fill_w = ((bw - 2) * pct) / 100;
    if (fill_w > 0) epaper_draw_rect(x + 1, y + 1, fill_w, bh - 2, 1);
}

static void draw_status_bar(void)
{
    draw_wifi_icon(2, 2, wifi_ok);
    epaper_draw_text(16, 1, status_text, 8);
    draw_battery_icon(DISPLAY_WIDTH - 52, 2, battery);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", battery);
    epaper_draw_text(DISPLAY_WIDTH - 30, 1, pct, 8);
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
    epaper_draw_text(10, 12, "Listening...", 16);
    epaper_draw_text(10, DISPLAY_HEIGHT - 16, "BACK=cancel", 8);
    draw_status_bar();
    epaper_partial_refresh();
}

void ui_update_recording_viz(int32_t energy)
{
    int bar_count = 12;
    int bar_w = 14;
    int gap = 2;
    int total_w = bar_count * (bar_w + gap) - gap;
    int start_x = (DISPLAY_WIDTH - total_w) / 2;
    int max_h = 55;
    int base_y = 55;

    for (int i = 0; i < bar_count; i++) {
        int h = (energy * max_h) / 30000;
        if (h < 2) h = 2;
        if (h > max_h) h = max_h;
        int x = start_x + i * (bar_w + gap);
        epaper_draw_rect(x, base_y + (max_h - h), bar_w, h, 1);
    }
    epaper_partial_refresh();
}

static const int SPOKES[8][2] = {
    {0, -28}, {20, -20}, {28, 0}, {20, 20},
    {0, 28}, {-20, 20}, {-28, 0}, {-20, -20},
};

void ui_show_processing_screen(void)
{
    epaper_clear();
    epaper_draw_text(10, 12, "Thinking...", 16);
    draw_status_bar();
    epaper_partial_refresh();
}

void ui_update_processing_anim(int frame)
{
    int cx = DISPLAY_WIDTH / 2;
    int cy = 80;
    int n = frame % 8;

    epaper_draw_line(cx, cy, cx + SPOKES[n][0], cy + SPOKES[n][1]);
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

static void draw_moon_icon(int x, int y)
{
    int r = 5;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r && d2 >= (r - 1) * (r - 1))
                epaper_draw_pixel(x + dx, y + dy, 0);
        }
    }
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = 0; dx <= 2; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= 3 * 3) epaper_draw_pixel(x + dx, y + dy, 1);
        }
    }
}

void ui_show_sleep_screen(void)
{
    epaper_clear();
    draw_moon_icon(DISPLAY_WIDTH / 2 - 30, DISPLAY_HEIGHT / 2 - 10);
    epaper_draw_text(DISPLAY_WIDTH / 2 - 30, DISPLAY_HEIGHT / 2 + 10, "Sleeping...", 12);
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
