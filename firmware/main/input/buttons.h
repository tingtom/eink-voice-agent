// Button input
#pragma once
#include <stdint.h>

typedef enum {
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_SELECT,
    BUTTON_BACK,
    BUTTON_COUNT
} button_id_t;

typedef void (*button_callback_t)(button_id_t button);

void buttons_init(void);
void button_set_callback(button_callback_t cb);
void button_set_longpress_callback(button_callback_t cb, uint32_t threshold_ms);
