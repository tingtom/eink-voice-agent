#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "esp_log.h"
#include "app_config.h"
#include "epaper_driver.h"
#include "power_mgmt.h"

static const char *TAG = "UI";
static char status_text[32] = {0};
static bool wifi_ok = false;
static bool hermes_ok = false;
static uint8_t battery = 0;


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

static void draw_offline_text(int x, int y)
{
    epaper_draw_text(x, y, "offline", 8);
}

static void draw_hermes_icon(int x, int y, bool connected)
{
    int s = 8;
    if (connected) {
        epaper_draw_rect(x, y, s, s, 1);
        epaper_clear_rect(x + 1, y + 1, s - 2, s - 2);
        epaper_draw_pixel(x + 2, y + 3, 0);
        epaper_draw_pixel(x + 5, y + 3, 0);
        epaper_draw_pixel(x + 3, y + 5, 0);
        epaper_draw_pixel(x + 4, y + 5, 0);
    } else {
        epaper_draw_rect(x, y, s, s, 0);
        epaper_draw_pixel(x + 2, y + 3, 1);
        epaper_draw_pixel(x + 5, y + 3, 1);
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

void ui_draw_button_help(const char *top, const char *bottom)
{
    if (top) {
        int tw = epaper_text_width(top, 8);
        epaper_draw_text(DISPLAY_WIDTH - tw - 4, DISPLAY_HEIGHT - 24, top, 8);
    }
    if (bottom) {
        int tw = epaper_text_width(bottom, 8);
        epaper_draw_text(DISPLAY_WIDTH - tw - 4, DISPLAY_HEIGHT - 16, bottom, 8);
    }
}

static void draw_status_bar(void)
{
    int x = 2;
    draw_wifi_icon(x, 0, wifi_ok);
    x += 22;
    if (!hermes_ok) {
        int tw = epaper_text_width("offline", 8);
        draw_offline_text((DISPLAY_WIDTH - tw) / 2, 3);
    }
    uint8_t bat_pct = power_get_battery_pct();
    draw_battery_icon(DISPLAY_WIDTH - 45, 3, bat_pct);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", bat_pct);
    epaper_draw_text(DISPLAY_WIDTH - 22, 4, pct, 8);
}

void ui_init(void)
{
    epaper_clear();
    ESP_LOGI(TAG, "UI initialized");
}

void ui_show_boot_screen(const char *device_name)
{
    epaper_clear();
    int tw1 = epaper_text_width(device_name, 16);
    int tw2 = epaper_text_width("Connecting...", 12);
    int cx1 = (DISPLAY_WIDTH - tw1) / 2;
    int cx2 = (DISPLAY_WIDTH - tw2) / 2;
    epaper_draw_text(cx1, DISPLAY_HEIGHT / 2 - 16, device_name, 16);
    epaper_draw_text(cx2, DISPLAY_HEIGHT / 2 + 8, "Connecting...", 12);
    epaper_partial_refresh();  // partial refresh to avoid battery brownout
}

void ui_show_home_screen(void)
{
    epaper_clear();
    int tw1 = epaper_text_width("Say 'Hi Jeff'", 12);
    epaper_draw_text((DISPLAY_WIDTH - tw1) / 2, 60, "Say 'Hi Jeff'", 12);
    draw_status_bar();
    ui_draw_button_help("hold to listen", NULL);
    epaper_draw_text(2, DISPLAY_HEIGHT - 8, "Select mode via buttons", 6);
    epaper_partial_refresh();
}

void ui_show_menu(const char **items, int count, int selected)
{
    epaper_clear();
    int line_h = 16;
    int start_y = (DISPLAY_HEIGHT - count * line_h) / 2;
    for (int i = 0; i < count; i++) {
        char line[32];
        if (i == selected) {
            snprintf(line, sizeof(line), "> %s", items[i]);
        } else {
            snprintf(line, sizeof(line), "  %s", items[i]);
        }
        epaper_draw_text(10, start_y + i * line_h, line, 12);
    }
    draw_status_bar();
    int tw;
    tw = epaper_text_width("up -", 8);
    epaper_draw_text(DISPLAY_WIDTH - tw - 4, DISPLAY_HEIGHT - 40, "up -", 8);
    tw = epaper_text_width("sel -", 8);
    epaper_draw_text(DISPLAY_WIDTH - tw - 4, DISPLAY_HEIGHT - 32, "sel -", 8);
    tw = epaper_text_width("down -", 8);
    epaper_draw_text(DISPLAY_WIDTH - tw - 4, DISPLAY_HEIGHT - 16, "down -", 8);
    
    // Draw description of selected item at bottom
    const char *desc = "";
    if (selected < count) {
        if (strcmp(items[selected], "WiFi IP") == 0) desc = "Shows WiFi IP address";
        else if (strcmp(items[selected], "Battery") == 0) desc = "Battery level and voltage";
        else if (strcmp(items[selected], "Storage") == 0) desc = "SD card free/total space";
        else if (strcmp(items[selected], "Manual Sync") == 0) desc = "Upload recordings";
        else if (strcmp(items[selected], "Voice Agent") == 0) desc = "Chat with Jeff";
        else if (strcmp(items[selected], "Transcribe") == 0) desc = "Speech to text";
        else if (strcmp(items[selected], "Voice Note") == 0) desc = "Record voice notes";
        else if (strcmp(items[selected], "Todo List") == 0) desc = "Voice todo items";
        else if (strcmp(items[selected], "Games") == 0) desc = "Mini games";
        else if (strcmp(items[selected], "View Notes") == 0) desc = "Listen to recordings";
        else if (strcmp(items[selected], "Settings") == 0) desc = "Device info & sync";
    }
    epaper_draw_text(2, DISPLAY_HEIGHT - 8, desc, 6);
    epaper_partial_refresh();
}

#define VIZ_BAR_COUNT 16
#define VIZ_BAR_W 10
#define VIZ_GAP 2
#define VIZ_MAX_H 40
#define VIZ_Y 80

static int viz_buf[VIZ_BAR_COUNT];
static int viz_prev[VIZ_BAR_COUNT];
static int recording_frame = 0;

void ui_show_recording_screen(void)
{
    epaper_clear();
    int tw = epaper_text_width("Listening...", 12);
    epaper_draw_text((DISPLAY_WIDTH - tw) / 2, 55, "Listening...", 12);
    draw_status_bar();
    ui_draw_button_help("cancel -", NULL);
    recording_frame = 0;
    for (int i = 0; i < VIZ_BAR_COUNT; i++) {
        viz_buf[i] = 2;  // minimum height so all bars are visible from the start
        viz_prev[i] = 0;
    }
    epaper_partial_refresh();
}

void ui_update_recording_viz(int32_t energy)
{
    int total_w = VIZ_BAR_COUNT * (VIZ_BAR_W + VIZ_GAP) - VIZ_GAP;
    int start_x = (DISPLAY_WIDTH - total_w) / 2;

    // Shift buffer left — oldest falls off, new data enters on the right
    for (int i = 0; i < VIZ_BAR_COUNT - 1; i++) {
        viz_buf[i] = viz_buf[i + 1];
    }
    int h = (energy * VIZ_MAX_H) / 500;
    if (h < 2) h = 2;
    if (h > VIZ_MAX_H) h = VIZ_MAX_H;
    viz_buf[VIZ_BAR_COUNT - 1] = h;

    // Draw bars left-to-right (oldest → newest), only redraw changed pixels
    for (int i = 0; i < VIZ_BAR_COUNT; i++) {
        int val = viz_buf[i];
        int x = start_x + i * (VIZ_BAR_W + VIZ_GAP);
        if (val != viz_prev[i]) {
            if (viz_prev[i] > 0)
                epaper_clear_rect(x, VIZ_Y + (VIZ_MAX_H - viz_prev[i]), VIZ_BAR_W, viz_prev[i]);
            epaper_draw_rect(x, VIZ_Y + (VIZ_MAX_H - val), VIZ_BAR_W, val, 1);
            viz_prev[i] = val;
        }
    }

    // NOTE: epaper_partial_refresh() removed from here — it blocks for ~500ms
    // and starves the audio capture task.  Display will update when recording
    // stops and a full/partial refresh is triggered elsewhere.
}

void ui_show_processing_screen(void)
{
    epaper_clear();
    int tw = epaper_text_width("Thinking...", 16);
    epaper_draw_text((DISPLAY_WIDTH - tw) / 2, 25, "Thinking...", 16);
    draw_status_bar();
    ui_draw_button_help("cancel -", NULL);
    epaper_partial_refresh();
}

void ui_update_processing_anim(int frame)
{
    int cx = DISPLAY_WIDTH / 2;
    int cy = VIZ_Y + VIZ_MAX_H / 2;  // Center vertically with recording viz
    
    // Rotating dot animation - 8 frames around circular path
    static int prev_px = -100, prev_py = -100;
    int phase = frame % 8;
    
    // Clear previous dot position
    if (prev_px >= 0 && prev_py >= 0) {
        epaper_clear_rect(prev_px - 3, prev_py - 3, 6, 6);
    }
    
    // Draw single rotating dot at 45-degree positions
    float angle = (phase * 45.0f) * 3.14159f / 180.0f;
    int r = 14;
    int px = cx + (int)(r * cosf(angle));
    int py = cy + (int)(r * sinf(angle));
    
    epaper_draw_rect(px - 2, py - 2, 4, 4, 1);
    prev_px = px;
    prev_py = py;
    
    epaper_partial_refresh();
}

void ui_show_response(const char *text)
{
    epaper_clear();
    epaper_draw_text(10, 20, text, 12);
    draw_status_bar();
    ui_draw_button_help(NULL, "back -");
    epaper_partial_refresh();
}

void ui_show_error(const char *message)
{
    epaper_clear();
    epaper_draw_text(10, 60, "Error:", 16);
    epaper_draw_text(10, 80, message, 12);
    ui_draw_button_help(NULL, "back -");
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
    ui_draw_button_help("up -", "down -");
    epaper_full_refresh();
}

static void draw_battery_icon_charging(int x, int y, int pct)
{
    int bw = 18, bh = 8;
    epaper_draw_rect(x, y, bw, bh, 0);
    epaper_draw_rect(x + bw, y + 2, 2, bh - 4, 1);
    int fill_w = ((bw - 2) * pct) / 100;
    if (fill_w > 0) epaper_draw_rect(x + 1, y + 1, fill_w, bh - 2, 1);
    int cx = x + bw / 2;
    int cy = y + bh / 2;
    epaper_draw_line(cx, cy - 3, cx - 2, cy);
    epaper_draw_line(cx - 2, cy, cx + 2, cy);
    epaper_draw_line(cx + 2, cy, cx - 1, cy + 3);
}

void ui_show_docked_screen(void)
{
    epaper_clear();
    uint8_t bat_pct = power_get_battery_pct();
    draw_battery_icon_charging(DISPLAY_WIDTH / 2 - 9, 40, bat_pct);
    int tw = epaper_text_width("Sleep & Charge", 12);
    epaper_draw_text((DISPLAY_WIDTH - tw) / 2, 70, "Sleep & Charge", 12);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", bat_pct);
    int tw2 = epaper_text_width(pct, 16);
    epaper_draw_text((DISPLAY_WIDTH - tw2) / 2, 88, pct, 16);
    ui_draw_button_help("hold to exit", NULL);
    epaper_partial_refresh();
}

void ui_show_sleep_screen(void)
{
    epaper_clear();
    epaper_draw_text(70, 50, "z", 8);
    epaper_draw_text(85, 80, "zz", 14);
    epaper_draw_text(100, 110, "zzz", 21);
    ui_draw_button_help(NULL, "wake -");
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
    if (pct > 100) pct = 100;
    if (pct == battery) return;
    battery = pct;
    epaper_clear_rect(DISPLAY_WIDTH - 45, 2, 44, 11);
    uint8_t bat_pct = power_get_battery_pct();
    draw_battery_icon(DISPLAY_WIDTH - 45, 3, bat_pct);
    char pct_str[8];
    snprintf(pct_str, sizeof(pct_str), "%d%%", battery);
    epaper_draw_text(DISPLAY_WIDTH - 22, 4, pct_str, 8);
    epaper_partial_refresh();
}

void ui_update_wifi_status(bool connected)
{
    if (wifi_ok == connected) return;
    wifi_ok = connected;
    epaper_clear_rect(2, 2, DISPLAY_WIDTH - 4, 9);
    draw_status_bar();
    epaper_partial_refresh();
}

void ui_update_hermes_status(bool connected)
{
    if (hermes_ok == connected) return;
    hermes_ok = connected;
    epaper_clear_rect(2, 2, DISPLAY_WIDTH - 4, 9);
    draw_status_bar();
    epaper_partial_refresh();
}

bool ui_is_hermes_connected(void)
{
    return hermes_ok;
}

void ui_set_status_text(const char *text)
{
    strncpy(status_text, text, sizeof(status_text) - 1);
    status_text[sizeof(status_text) - 1] = '\0';
}
