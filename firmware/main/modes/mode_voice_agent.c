#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "audio_pipeline.h"
#include "ui_manager.h"
#include "ws_client.h"

static const char *TAG = "MODE_AGENT";
static bool active = false;

void mode_voice_agent_start(void)
{
    ESP_LOGI(TAG, "Voice agent mode started");
    active = true;
    ui_show_recording_screen();
    audio_pipeline_start_recording(MODE_AGENT);
}

void mode_voice_agent_stop(void)
{
    ESP_LOGI(TAG, "Voice agent mode stopped");
    active = false;
    audio_pipeline_stop_recording();
    ui_show_home_screen();
}

void mode_voice_agent_handle_response(const char *text)
{
    if (!active) return;
    ui_show_response(text);
}
