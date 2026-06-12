#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "app_config.h"
#include "buttons.h"

static const char *TAG = "BUTTONS";

static const int button_gpios[BUTTON_COUNT] = {
    BUTTON_UP_GPIO,
    BUTTON_DOWN_GPIO,
    BUTTON_SELECT_GPIO,
    BUTTON_BACK_GPIO,
};

static button_callback_t user_callback = NULL;
static bool last_state[BUTTON_COUNT] = {false};

static void buttons_scan_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        for (int i = 0; i < BUTTON_COUNT; i++) {
            bool pressed = gpio_get_level(button_gpios[i]) == 0;
            if (pressed && !last_state[i]) {
                vTaskDelay(pdMS_TO_TICKS(20));
                if (gpio_get_level(button_gpios[i]) == 0) {
                    ESP_LOGI(TAG, "Button %d pressed", i);
                    if (user_callback) {
                        user_callback((button_id_t)i);
                    }
                }
            }
            last_state[i] = pressed;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(30));
    }
}

void buttons_init(void)
{
    xTaskCreate(buttons_scan_task, "buttons", 2048, NULL, 10, NULL);
    ESP_LOGI(TAG, "Buttons initialized");
}

void button_set_callback(button_callback_t cb)
{
    user_callback = cb;
}
