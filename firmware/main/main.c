#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_sleep.h"

#include "app_config.h"
#include "system_init.h"
#include "wifi_manager.h"
#include "epaper_driver.h"
#include "ui_manager.h"
#include "buttons.h"
#include "audio_pipeline.h"
#include "vad.h"
#include "mic_driver.h"
#include "ws_client.h"
#include "power_mgmt.h"
#include "ulp_vad_shared.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();

    if (wake_cause == ESP_SLEEP_WAKEUP_ULP) {
        uint32_t reason = ulp_vad_data.wake_reason;
        ulp_vad_data.flags &= ~ULP_FLAG_CPU_WAKE_REQUEST;
        ESP_LOGI(TAG, "ULP wake: reason=%lu, buttons=0x%lx", reason, (unsigned long)ulp_vad_data.button_state);

        if (reason == ULP_WAKE_TIMER) {
            epaper_init();
            ui_init();
            mic_init();
            mic_start();
            vad_init();

            int16_t buf[256];
            size_t read = 0;
            int energy_ok_count = 0;

            for (int i = 0; i < 6; i++) {
                if (mic_read(buf, 256, &read) == ESP_OK && read > 0) {
                    int32_t energy = vad_compute_energy(buf, read);
                    if (energy >= AUDIO_VAD_THRESHOLD) energy_ok_count++;
                    ESP_LOGD(TAG, "Burst energy[%d]: %ld", i, (long)energy);
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            mic_stop();

            if (energy_ok_count >= 3) {
                ESP_LOGI(TAG, "Voice detected, staying awake");
            } else {
                ESP_LOGI(TAG, "No voice detected, returning to sleep");
                epaper_sleep();
                power_enter_deep_sleep();
                return;
            }
        }

        if (reason == ULP_WAKE_BUTTON) {
            ESP_LOGI(TAG, "Button wake (mask=0x%lx)", (unsigned long)ulp_vad_data.button_state);
        }
    }

    ESP_LOGI(TAG, "Device: %s", DEVICE_NAME);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    system_init();
    epaper_init();
    ui_init();
    ui_show_boot_screen(DEVICE_NAME);

    wifi_init();
    wifi_connect(WIFI_SSID, WIFI_PASSWORD);

    int retries = 0;
    while (!wifi_is_connected() && retries < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retries++;
    }

    if (wifi_is_connected()) {
        ESP_LOGI(TAG, "WiFi connected");
        ui_update_status_bar(true, power_get_battery_pct());
    } else {
        ESP_LOGW(TAG, "WiFi connection failed, continuing offline");
        ui_show_error("WiFi failed");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    audio_pipeline_init();
    ws_client_init(HERMES_WS_URL, DEVICE_AUTH_TOKEN);
    buttons_init();
    ui_show_home_screen();
    power_ulp_load();

    ESP_LOGI(TAG, "Initialization complete, entering main loop");

    while (1) {
        uint8_t battery = power_get_battery_pct();
        ui_update_battery(battery);
        ui_update_wifi_status(wifi_is_connected());

        if (power_should_sleep()) {
            ESP_LOGI(TAG, "Entering deep sleep with ULP...");
            ui_show_sleep_screen();
            vTaskDelay(pdMS_TO_TICKS(100));
            power_enter_deep_sleep();
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
