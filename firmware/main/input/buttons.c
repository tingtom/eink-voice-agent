#include <stdbool.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "app_config.h"
#include "buttons.h"

static const char *TAG = "BUTTONS";

// Map physical buttons to logical IDs with press-type semantics:
//   Short press (on release): BOOT->SELECT(UP), PWR->BACK(DOWN)
//   Long press  (on hold):    BOOT->SELECT(CONFIRM), PWR->BACK(SLEEP)
static const int button_gpios[BUTTON_COUNT] = {
    BUTTON_BOOT_GPIO,  // BUTTON_SELECT
    BUTTON_PWR_GPIO,   // BUTTON_BACK
    -1,                // BUTTON_UP (unused)
    -1,                // BUTTON_DOWN (unused)
};

static button_callback_t user_callback = NULL;
static button_callback_t longpress_callback = NULL;
static uint32_t longpress_threshold_ms = 0;

static bool last_state[BUTTON_COUNT] = {false};
static int64_t press_start_us[BUTTON_COUNT] = {0};
static bool longpress_fired[BUTTON_COUNT] = {false};

static void buttons_scan_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        for (int i = 0; i < BUTTON_COUNT; i++) {
            if (button_gpios[i] < 0) continue;

            bool pressed = gpio_get_level(button_gpios[i]) == 0;

            if (pressed) {
                if (!last_state[i]) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                    if (gpio_get_level(button_gpios[i]) == 0) {
                        ESP_LOGI(TAG, "Button %d pressed", i);
                        press_start_us[i] = esp_timer_get_time();
                        longpress_fired[i] = false;
                    }
                } else if (longpress_callback && !longpress_fired[i]) {
                    if (esp_timer_get_time() - press_start_us[i] >=
                        (int64_t)longpress_threshold_ms * 1000LL) {
                        longpress_fired[i] = true;
                        ESP_LOGI(TAG, "Button %d long-pressed", i);
                        longpress_callback((button_id_t)i);
                    }
                }
            } else {
                if (last_state[i] && !longpress_fired[i] && user_callback) {
                    user_callback((button_id_t)i);
                }
                longpress_fired[i] = false;
            }

            last_state[i] = pressed;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(30));
    }
}

void buttons_init(void)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (button_gpios[i] < 0) continue;
        gpio_reset_pin(button_gpios[i]);
        gpio_set_direction(button_gpios[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(button_gpios[i], GPIO_PULLUP_ONLY);
    }
    xTaskCreate(buttons_scan_task, "buttons", 2048, NULL, 10, NULL);
    ESP_LOGI(TAG, "Buttons initialized");
}

void button_set_callback(button_callback_t cb)
{
    user_callback = cb;
}

void button_set_longpress_callback(button_callback_t cb, uint32_t threshold_ms)
{
    longpress_callback = cb;
    longpress_threshold_ms = threshold_ms;
}
