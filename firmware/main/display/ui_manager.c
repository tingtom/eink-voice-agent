#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "app_config.h"
#include "epaper_driver.h"

static const char *TAG = "UI";
static char status_text[32] = {0};
static bool wifi_ok = false;
static bool hermes_ok = false;
static uint8_t battery = 0;
static bool driving_mode = false;

static void draw_wifi_icon(int x, int y, bool connected)
{
    int bar_w = 3;
    int gap = 2;
    int heights[4] = {3, 5, 7, 9};
    int base_y = y + 12;
    for (int i = 0; i < 4; i++) {
        int bx = x + i * (bar_w + gap);
        int bh = heights[i];
        if (connected) {
            epaper_draw_rect(bx, base_y - bh, bar_w, bh, 1);
        } else {
            epaper_draw_rect(bx, base_y - bh, bar_w, bh, 0);
        }
    }
}

static void draw_hermes_icon(int x, int y, bool connected)
{
    int cx = x + 4;
    int cy = y + 4;
    int r = 4;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > (r - 1) * (r - 1) && dx * dx + dy * dy <= r * r + 1) {
                epaper_draw_pixel(cx + dx, cy + dy, 0);
            }
        }
    }
    if (connected) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx * dx + dy * dy <= 4) {
                    epaper_draw_pixel(cx + dx, cy + dy, 0);
                }
            }
        }
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
    int x = 2;
    draw_wifi_icon(x, 2, wifi_ok);
    x += 16;
    draw_hermes_icon(x, 2, hermes_ok);
    x += 12;
    if (driving_mode) {
        epaper_draw_text(x, 1, "[D]", 8);
        x += 20;
    }
    epaper_draw_text(x, 1, status_text, 8);
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
    int tw1 = epaper_text_width(device_name, 12);
    int tw2 = epaper_text_width("Connecting...", 12);
    int cx1 = (DISPLAY_WIDTH - tw1) / 2;
    int cx2 = (DISPLAY_WIDTH - tw2) / 2;
    epaper_draw_text(cx1, DISPLAY_HEIGHT / 2 - 16, device_name, 12);
    epaper_draw_text(cx2, DISPLAY_HEIGHT / 2 + 8, "Connecting...", 12);
    epaper_full_refresh();
}

void ui_show_home_screen(void)
{
    epaper_clear();
    int tw1 = epaper_text_width("Say 'Hi Jeff'", 12);
    int tw2 = epaper_text_width("or press SELECT", 12);
    epaper_draw_text((DISPLAY_WIDTH - tw1) / 2, 60, "Say 'Hi Jeff'", 12);
    epaper_draw_text((DISPLAY_WIDTH - tw2) / 2, 80, "or press SELECT", 12);
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
    int tw = epaper_text_width("Listening...", 16);
    epaper_draw_text((DISPLAY_WIDTH - tw) / 2, 35, "Listening...", 16);
    epaper_draw_text(10, DISPLAY_HEIGHT - 16, "long SELECT=cancel", 8);
    draw_status_bar();
    epaper_partial_refresh();
}

static int prev_heights[12] = {0};

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
        float t = (float)(i + 1) / bar_count;
        int h = (int)((energy * max_h * t) / 30000);
        if (h < 2) h = 2;
        if (h > max_h) h = max_h;

        int x = start_x + i * (bar_w + gap);
        if (h > prev_heights[i]) {
            epaper_draw_rect(x, base_y + (max_h - h), bar_w, h, 1);
        } else if (h < prev_heights[i]) {
            epaper_draw_rect(x, base_y + (max_h - prev_heights[i]), bar_w, prev_heights[i], 0);
            epaper_draw_rect(x, base_y + (max_h - h), bar_w, h, 1);
        }
        prev_heights[i] = h;
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
    int tw = epaper_text_width("Thinking...", 16);
    epaper_draw_text((DISPLAY_WIDTH - tw) / 2, 12, "Thinking...", 16);
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

void ui_show_provisioning_screen(const char *ap_name, const char *url)
{
    epaper_clear();
    epaper_draw_text(DISPLAY_WIDTH / 2 - 40, 12, "Setup Mode", 16);
    epaper_draw_text(10, 55, "Connect to WiFi:", 12);
    epaper_draw_text(10, 73, ap_name, 8);
    epaper_draw_text(10, 90, "Then open:", 12);
    epaper_draw_text(10, 108, url, 12);
    epaper_full_refresh();
}

static void draw_car_icon(int x, int y)
{
    epaper_draw_rect(x + 4, y + 8, 28, 10, 1);
    epaper_draw_rect(x + 10, y + 2, 16, 6, 1);
    epaper_draw_rect(x + 12, y + 4, 5, 3, 0);
    epaper_draw_rect(x + 19, y + 4, 5, 3, 0);
    epaper_draw_rect(x + 6, y + 18, 5, 3, 1);
    epaper_draw_rect(x + 25, y + 18, 5, 3, 1);
}

static void draw_charging_bolt(int x, int y)
{
    epaper_draw_line(x + 4, y, x + 1, y + 10);
    epaper_draw_line(x + 1, y + 10, x + 6, y + 10);
    epaper_draw_line(x + 6, y + 10, x + 2, y + 20);
    epaper_draw_line(x + 2, y + 20, x + 7, y + 10);
    epaper_draw_line(x + 7, y + 10, x + 2, y + 10);
    epaper_draw_line(x + 2, y + 10, x + 5, y);
}

void ui_show_docked_screen(void)
{
    epaper_clear();
    draw_charging_bolt(DISPLAY_WIDTH / 2 - 4, 40);
    int tw = epaper_text_width("Sleep & Charge", 12);
    epaper_draw_text((DISPLAY_WIDTH - tw) / 2, 80, "Sleep & Charge", 12);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", battery);
    int tw2 = epaper_text_width(pct, 16);
    epaper_draw_text((DISPLAY_WIDTH - tw2) / 2, 100, pct, 16);
    epaper_full_refresh();
}

void ui_show_driving_screen(void)
{
    epaper_clear();
    draw_car_icon(DISPLAY_WIDTH / 2 - 18, 16);
    int tw = epaper_text_width("Driving Mode", 12);
    epaper_draw_text((DISPLAY_WIDTH - tw) / 2, 44, "Driving Mode", 12);
    epaper_draw_text(10, DISPLAY_HEIGHT - 16, "long SELECT=exit", 8);
    draw_status_bar();
    epaper_partial_refresh();
}

void ui_set_driving_mode(bool enabled)
{
    driving_mode = enabled;
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

void ui_update_status_bar(bool wifi_connected, bool hermes_connected, uint8_t battery_pct)
{
    wifi_ok = wifi_connected;
    hermes_ok = hermes_connected;
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

void ui_update_hermes_status(bool connected)
{
    hermes_ok = connected;
}

void ui_set_status_text(const char *text)
{
    strncpy(status_text, text, sizeof(status_text) - 1);
    status_text[sizeof(status_text) - 1] = '\0';
}
