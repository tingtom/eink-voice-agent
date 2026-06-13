#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "audio_pipeline.h"
#include "ui_manager.h"

static const char *TAG = "MODE_TRANSCRIBE";
static bool active = false;

void mode_transcribe_start(void)
{
    ESP_LOGI(TAG, "Transcribe mode started");
    active = true;
    ui_show_recording_screen();
    audio_pipeline_start_recording(MODE_TRANSCRIBE);
}

void mode_transcribe_stop(void)
{
    ESP_LOGI(TAG, "Transcribe mode stopped");
    active = false;
    audio_pipeline_stop_recording();
    ui_show_home_screen();
}

void mode_transcribe_handle_result(const char *text)
{
    if (!active) return;
    ui_show_response(text);
}
