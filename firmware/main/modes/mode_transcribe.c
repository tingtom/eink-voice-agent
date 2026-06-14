#include <string.h>
#include <stdbool.h>
#include "mode_transcribe.h"
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
    audio_pipeline_stop_recording();
    audio_pipeline_send_end_recording();
    ui_show_processing_screen();
    audio_pipeline_start_processing();
    active = false;
}

void mode_transcribe_handle_response(const char *text)
{
    if (!active) return;
    audio_pipeline_stop_processing();
    ui_show_response(text);
}
