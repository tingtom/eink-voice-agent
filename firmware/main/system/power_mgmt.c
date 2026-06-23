#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "app_config.h"
#include "epaper_driver.h"
#include "mic_driver.h"

static const char *TAG = "POWER";

static int64_t last_activity_time = 0;
static bool initialized = false;
static adc_oneshot_unit_handle_t adc_handle = NULL;
static int32_t wake_count = 0;

void power_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &chan_cfg));

    last_activity_time = esp_timer_get_time();
    initialized = true;

    // Wake count — persist across deep sleep
    nvs_handle_t nvs;
    if (nvs_open("system", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_get_i32(nvs, "wake_count", &wake_count);
        wake_count++;
        nvs_set_i32(nvs, "wake_count", wake_count);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "Power management initialized (wake #%" PRId32 ")", wake_count);
}

static int read_battery_mv(void)
{
    int raw = 0;
    esp_err_t ret = adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &raw);
    if (ret != ESP_OK) return BATTERY_MIN_MV;
    return (raw * BATTERY_MAX_MV) / 4095;
}

uint8_t power_get_battery_pct(void)
{
    int mv = read_battery_mv();
    if (mv <= BATTERY_MIN_MV) return 0;
    if (mv >= BATTERY_MAX_MV) return 100;
    return (uint8_t)((mv - BATTERY_MIN_MV) * 100 / (BATTERY_MAX_MV - BATTERY_MIN_MV));
}

bool power_is_charging(void)
{
    return false;
}

int32_t power_get_wake_count(void)
{
    return wake_count;
}

bool power_should_sleep(void)
{
    if (!initialized) return false;
    int64_t now = esp_timer_get_time();
    int64_t idle_us = now - last_activity_time;
    return idle_us > (int64_t)SLEEP_TIMEOUT_MS * 1000;
}

void power_mark_activity(void)
{
    last_activity_time = esp_timer_get_time();
}

static void power_before_sleep(void)
{
    epaper_sleep();
    mic_stop();
}

void power_enter_deep_sleep(uint64_t wake_time_us)
{
    ESP_LOGI(TAG, "Entering deep sleep (wake_time=%llu us)", (unsigned long long)wake_time_us);
    power_before_sleep();

    if (wake_time_us > 0) {
        esp_sleep_enable_timer_wakeup(wake_time_us);
    }
    gpio_wakeup_enable(BUTTON_BOOT_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    esp_deep_sleep_start();
}
