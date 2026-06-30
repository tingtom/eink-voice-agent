#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "epaper.h"
#include "epaper_config.h"
#include "app_config.h"
#include "font_5x7.h"

static const char *TAG = "EPAPER";

static epd_handle_t epd_handle = NULL;
static uint8_t *fb = NULL;
static bool initialized = false;

static void set_pixel(int x, int y, int color)
{
    if (!fb || x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) return;
    int idx = y * (DISPLAY_WIDTH / 8) + (x / 8);
    int bit = 7 - (x % 8);
    if (color == 0)
        fb[idx] &= ~(1 << bit);
    else
        fb[idx] |= (1 << bit);
}

void epaper_init(void)
{
    epd_config_t cfg = {
        .pins = {
            .busy = EPAPER_BUSY_GPIO,
            .rst = EPAPER_RST_GPIO,
            .dc = EPAPER_DC_GPIO,
            .cs = EPAPER_CS_GPIO,
            .sck = EPAPER_CLK_GPIO,
            .mosi = EPAPER_MOSI_GPIO,
        },
        .spi = {
            .host = EPAPER_SPI_HOST,
            .speed_hz = 10 * 1000 * 1000,
        },
        .panel = {
            .type = EPD_PANEL_GDEY0154D67,
            .width = 0,
            .height = 0,
        },
    };

    ESP_ERROR_CHECK(epd_init(&cfg, &epd_handle));
    fb = epd_get_framebuffer(epd_handle);
    initialized = true;
    ESP_LOGI(TAG, "E-paper initialized (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void epaper_clear(void)
{
    if (fb) {
        memset(fb, 0xFF, DISPLAY_WIDTH * DISPLAY_HEIGHT / 8);
    }
}

void epaper_full_refresh(void)
{
    epd_update(epd_handle, fb, EPD_UPDATE_FULL);
}

void epaper_partial_refresh(void)
{
    epd_update(epd_handle, fb, EPD_UPDATE_PARTIAL);
}

void epaper_draw_pixel(int x, int y, int color)
{
    set_pixel(x, y, color);
}

static void draw_char_5x7(int x, int y, char c, int color, int scale)
{
    if (c < FONT_5X7_FIRST_CHAR || c > FONT_5X7_LAST_CHAR) return;
    int idx = (c - FONT_5X7_FIRST_CHAR) * FONT_5X7_BYTES_PER_CHAR;
    for (int row = 0; row < FONT_5X7_HEIGHT; row++) {
        uint8_t bits = font_5x7_data[idx + row];
        for (int col = 0; col < FONT_5X7_WIDTH; col++) {
            if (bits & (1 << (4 - col))) {
                if (scale <= 1) {
                    set_pixel(x + col, y + row, color);
                } else {
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            set_pixel(x + col * scale + sx, y + row * scale + sy, color);
                }
            }
        }
    }
}

static int char_width(int scale)
{
    return (FONT_5X7_WIDTH + 1) * scale;
}

static int char_height(int scale)
{
    return (FONT_5X7_HEIGHT + 1) * scale;
}

void epaper_draw_text(int x, int y, const char *text, int font_size)
{
    int scale;
    if (font_size <= 8) scale = 1;
    else if (font_size <= 14) scale = 2;
    else scale = 3;

    int cw = char_width(scale);
    int ch = char_height(scale);
    int px = x;
    int py = y;

    while (*text) {
        if (*text == '\n') {
            px = x;
            py += ch;
        } else {
            draw_char_5x7(px, py, *text, 0, scale);
            px += cw;
        }
        text++;
    }
}

int epaper_text_width(const char *text, int font_size)
{
    int scale;
    if (font_size <= 8) scale = 1;
    else if (font_size <= 14) scale = 2;
    else scale = 3;
    return strlen(text) * char_width(scale);
}

void epaper_draw_line(int x1, int y1, int x2, int y2)
{
    int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int sx = (x2 > x1) ? 1 : -1;
    int sy = (y2 > y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        set_pixel(x1, y1, 0);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
}

static void draw_rect_helper(int x, int y, int w, int h, int fill)
{
    for (int py = y; py < y + h && py < DISPLAY_HEIGHT; py++) {
        for (int px = x; px < x + w && px < DISPLAY_WIDTH; px++) {
            if (fill || px == x || px == x + w - 1 || py == y || py == y + h - 1) {
                set_pixel(px, py, 0);
            }
        }
    }
}

void epaper_clear_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    for (int py = y; py < y + h && py < DISPLAY_HEIGHT; py++) {
        for (int px = x; px < x + w && px < DISPLAY_WIDTH; px++) {
            set_pixel(px, py, 1);
        }
    }
}

void epaper_draw_rect(int x, int y, int w, int h, int fill)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    draw_rect_helper(x, y, w, h, fill);
}

void epaper_sleep(void)
{
    if (initialized) {
        epd_sleep(epd_handle);
        initialized = false;
    }
}

void epaper_wakeup(void)
{
    if (!initialized) {
        epaper_init();
    }
}
