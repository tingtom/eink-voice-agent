#pragma once
#include <stdint.h>

void ui_init(void);
void ui_show_boot_screen(const char *device_name);
void ui_show_home_screen(void);
void ui_show_menu(const char **items, int count, int selected);
void ui_show_recording_screen(void);
void ui_update_recording_viz(int32_t energy);
void ui_show_processing_screen(void);
void ui_update_processing_anim(int frame);
void ui_show_response(const char *text);
void ui_show_error(const char *message);
void ui_show_sleep_screen(void);
void ui_show_docked_screen(void);
void ui_show_driving_screen(void);
void ui_update_status_bar(bool wifi_connected, uint8_t battery_pct);
void ui_update_battery(uint8_t pct);
void ui_update_wifi_status(bool connected);
void ui_set_status_text(const char *text);
void ui_set_driving_mode(bool enabled);
