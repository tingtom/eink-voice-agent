#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_sleep.h"

#include "app_config.h"
#include "nvs_flash.h"
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

static const char *TAG = "MAIN";

#define VAD_BURST_POLL_US  (200 * 1000)
#define VAD_BURST_SAMPLES  256

static bool handle_vad_burst(void)
{
    epaper_init();
    mic_init();
    mic_start();
    vad_init();

    int16_t buf[VAD_BURST_SAMPLES];
    int energy_ok = 0;

    for (int i = 0; i < 6; i++) {
        size_t read = 0;
        if (mic_read(buf, VAD_BURST_SAMPLES, &read) == ESP_OK && read > 0) {
            int32_t energy = vad_compute_energy(buf, read);
            if (energy >= AUDIO_VAD_THRESHOLD) energy_ok++;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    mic_stop();
    return energy_ok >= 3;
}

void app_main(void)
{
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();

    if (wake_cause == ESP_SLEEP_WAKEUP_TIMER) {
        if (handle_vad_burst()) {
            ESP_LOGI(TAG, "Voice detected, full wake");
            epaper_sleep();
            esp_restart();
        } else {
            ESP_LOGI(TAG, "No voice, back to sleep");
            power_enter_deep_sleep(VAD_BURST_POLL_US);
            return;
        }
    } else if (wake_cause == ESP_SLEEP_WAKEUP_GPIO) {
        ESP_LOGI(TAG, "Button wake");
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

    ESP_LOGI(TAG, "Initialization complete, entering main loop");

    while (1) {
        uint8_t battery = power_get_battery_pct();
        ui_update_battery(battery);
        ui_update_wifi_status(wifi_is_connected());

        if (power_should_sleep()) {
            ESP_LOGI(TAG, "Entering deep sleep...");
            ui_show_sleep_screen();
            vTaskDelay(pdMS_TO_TICKS(100));
            power_enter_deep_sleep(0);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
