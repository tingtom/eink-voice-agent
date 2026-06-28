// E-Paper display driver
#pragma once
#include "esp_err.h"
void epaper_init(void);
void epaper_clear(void);
void epaper_full_refresh(void);
void epaper_partial_refresh(void);
void epaper_draw_pixel(int x, int y, int color);
void epaper_draw_text(int x, int y, const char *text, int font_size);
int epaper_text_width(const char *text, int font_size);
void epaper_draw_line(int x1, int y1, int x2, int y2);
void epaper_draw_rect(int x, int y, int w, int h, int fill);
void epaper_clear_rect(int x, int y, int w, int h);
void epaper_sleep(void);
void epaper_wakeup(void);
