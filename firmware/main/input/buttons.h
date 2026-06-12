// Button input
#pragma once
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
