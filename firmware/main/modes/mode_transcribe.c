#include <string.h>
#include <stdbool.h>
#include "mode_transcribe.h"
#include "esp_log.h"
#include "audio_pipeline.h"
#include "ui_manager.h"
#include "wifi_manager.h"
#include "system_init.h"

static const char *TAG = "MODE_TRANSCRIBE";
static bool active = false;
static bool is_offline = false;

void mode_transcribe_start(void)
{
    ESP_LOGI(TAG, "Transcribe mode started");
    active = true;
    ui_show_recording_screen();
    board_power_led_on();

    if (wifi_is_connected()) {
        is_offline = false;
        audio_pipeline_start_recording(MODE_TRANSCRIBE);
    } else {
        is_offline = true;
        audio_pipeline_start_offline_recording(REC_TYPE_TRANSCRIBE);
    }
}

void mode_transcribe_stop(void)
{
    ESP_LOGI(TAG, "Transcribe mode stopped");

    if (is_offline) {
        audio_pipeline_stop_offline_recording();
        ui_show_response("Offline recording saved");
        ui_show_home_screen();
    } else {
        audio_pipeline_start_processing();  // Set BEFORE stopping to prevent spurious wake word checks
        ui_show_processing_screen();
        audio_pipeline_stop_recording();
        audio_pipeline_send_end_recording();
    }
}

void mode_transcribe_finish(void)
{
    if (!active) return;
    audio_pipeline_stop_processing();
    active = false;
    board_power_led_off();
    ui_show_home_screen();
}

void mode_transcribe_handle_response(const char *text)
{
    if (!active) return;
    audio_pipeline_stop_processing();
    ui_show_response(text);
}
