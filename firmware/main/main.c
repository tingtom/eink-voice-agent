/**
 * E-Ink Voice Agent — Main Entry Point
 *
 * Initializes all subsystems and starts the main application loop.
 * See docs/firmware-plan.md for architecture details.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "app_config.h"
#include "system_init.h"
#include "wifi_manager.h"
#include "epaper_driver.h"
#include "ui_manager.h"
#include "buttons.h"
#include "audio_pipeline.h"
#include "ws_client.h"
#include "power_mgmt.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "E-Ink Voice Agent starting...");
    ESP_LOGI(TAG, "Device: %s", DEVICE_NAME);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize system (GPIO, power management)
    system_init();

    // Initialize e-ink display
    epaper_init();
    ui_init();

    // Show boot screen
    ui_show_boot_screen(DEVICE_NAME);

    // Initialize WiFi
    wifi_init();
    wifi_connect(WIFI_SSID, WIFI_PASSWORD);

    // Wait for WiFi connection
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

    // Initialize audio pipeline (mic + wake word)
    audio_pipeline_init();

    // Initialize WebSocket client
    ws_client_init(HERMES_WS_URL, DEVICE_AUTH_TOKEN);

    // Initialize buttons
    buttons_init();

    // Show home screen
    ui_show_home_screen();

    ESP_LOGI(TAG, "Initialization complete, entering main loop");

    // Main loop — monitor system state, handle power management
    while (1) {
        // Check battery level periodically
        uint8_t battery = power_get_battery_pct();
        ui_update_battery(battery);

        // Check WiFi status
        ui_update_wifi_status(wifi_is_connected());

        // Enter deep sleep if idle for too long
        if (power_should_sleep()) {
            ESP_LOGI(TAG, "Entering deep sleep...");
            ui_show_sleep_screen();
            power_enter_deep_sleep(SLEEP_TIMEOUT_MS);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
