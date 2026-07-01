#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mode_voice_agent.h"
#include "esp_log.h"
#include "audio_pipeline.h"
#include "ui_manager.h"
#include "wifi_manager.h"
#include "system_init.h"

static const char *TAG = "MODE_AGENT";
static bool active = false;
static bool is_offline = false;

static void restart_recording_task(void *arg)
{
    (void)arg;
    // Brief pause so the user can see the response, then start listening again
    vTaskDelay(pdMS_TO_TICKS(1500));
    if (active && !audio_pipeline_is_docked()) {
        ESP_LOGI(TAG, "Restarting recording for next turn");
        ui_show_recording_screen();
        board_power_led_on();
        audio_pipeline_start_recording(MODE_AGENT);
    }
    vTaskDelete(NULL);
}

void mode_voice_agent_start(void)
{
    ESP_LOGI(TAG, "Voice agent mode started");
    active = true;
    ui_show_recording_screen();
    board_power_led_on();

    if (wifi_is_connected()) {
        is_offline = false;
        audio_pipeline_start_recording(MODE_AGENT);
    } else {
        is_offline = true;
        audio_pipeline_start_offline_recording(REC_TYPE_AGENT);
    }
}

void mode_voice_agent_stop(void)
{
    ESP_LOGI(TAG, "Voice agent mode stopped");

    if (is_offline) {
        audio_pipeline_stop_offline_recording();
        ui_show_response("Offline recording saved");
        ui_show_home_screen();
    } else {
        audio_pipeline_start_processing();  // Set BEFORE stopping to prevent spurious wake word checks
        ui_show_processing_screen("Thinking...");
        audio_pipeline_stop_recording();
        audio_pipeline_send_end_recording();
    }
}

void mode_voice_agent_finish(void)
{
    if (!active) return;
    audio_pipeline_stop_processing();
    active = false;
    board_power_led_off();
    ui_show_home_screen();
}

void mode_voice_agent_handle_response(const char *text)
{
    if (!active) return;
    audio_pipeline_stop_processing();
    ui_show_response(text);
    // Restart recording for back-and-forth conversation
    xTaskCreate(restart_recording_task, "restart_rec", 2048, NULL, 3, NULL);
}
