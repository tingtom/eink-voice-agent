#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "app_config.h"

static const char *TAG = "SYSTEM";

void system_init(void)
{
    ESP_LOGI(TAG, "Initializing system...");

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_UP_GPIO) |
                        (1ULL << BUTTON_DOWN_GPIO) |
                        (1ULL << BUTTON_SELECT_GPIO) |
                        (1ULL << BUTTON_BACK_GPIO) |
                        (1ULL << CHARGE_STATUS_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(SPK_ENABLE_GPIO, 0);
    gpio_config_t spk_en = {
        .pin_bit_mask = (1ULL << SPK_ENABLE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&spk_en);

    ESP_LOGI(TAG, "System initialized");
}
