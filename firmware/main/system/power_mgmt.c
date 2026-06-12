#include <stdio.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ulp_riscv.h"
#include "app_config.h"
#include "epaper_driver.h"
#include "mic_driver.h"
#include "ulp_vad_shared.h"

static const char *TAG = "POWER";

static int64_t last_activity_time = 0;
static bool initialized = false;
static adc_oneshot_unit_handle_t adc_handle = NULL;
static bool ulp_loaded = false;

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
    ESP_LOGI(TAG, "Power management initialized");
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
    return gpio_get_level(CHARGE_STATUS_GPIO) == 0;
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

esp_err_t power_ulp_load(void)
{
    if (ulp_loaded) return ESP_OK;

    esp_err_t err = ulp_riscv_load_and_run();
    if (err == ESP_OK) {
        ulp_loaded = true;
        ESP_LOGI(TAG, "ULP VAD coprocessor loaded and running");
    } else {
        ESP_LOGE(TAG, "Failed to load ULP coprocessor: %s", esp_err_to_name(err));
    }
    return err;
}

static void power_prepare_sleep(void)
{
    epaper_sleep();
    mic_stop();

    ulp_vad_data.flags &= ~ULP_FLAG_CPU_ACK;
    ulp_vad_data.button_state = 0;
    ulp_vad_data.wake_reason = ULP_WAKE_NONE;
}

static esp_sleep_source_t power_get_wake_source(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    switch (cause) {
        case ESP_SLEEP_WAKEUP_ULP:
            return ESP_SLEEP_WAKEUP_ULP;
        case ESP_SLEEP_WAKEUP_GPIO:
            return ESP_SLEEP_WAKEUP_GPIO;
        case ESP_SLEEP_WAKEUP_TIMER:
            return ESP_SLEEP_WAKEUP_TIMER;
        default:
            return ESP_SLEEP_WAKEUP_UNDEFINED;
    }
}

esp_sleep_source_t power_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep (ULP active)");
    power_prepare_sleep();

    esp_sleep_enable_ulp_wakeup();
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable(ULP_DEEP_SLEEP_WAKEUP_PIN, GPIO_INTR_LOW_LEVEL);

    esp_deep_sleep_start();
    return power_get_wake_source();
}

void power_enter_timer_sleep(uint32_t duration_ms)
{
    ESP_LOGI(TAG, "Entering timer sleep for %lu ms", (unsigned long)duration_ms);
    power_prepare_sleep();

    esp_sleep_enable_timer_wakeup(duration_ms * 1000);
    esp_sleep_enable_ulp_wakeup();
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable(ULP_DEEP_SLEEP_WAKEUP_PIN, GPIO_INTR_LOW_LEVEL);

    esp_deep_sleep_start();
}
