#include <string.h>
#include <stdbool.h>
#include "mode_todo.h"
#include "esp_log.h"
#include "audio_pipeline.h"
#include "ui_manager.h"

static const char *TAG = "MODE_TODO";
static bool active = false;

void mode_todo_start(void)
{
    ESP_LOGI(TAG, "Todo mode started");
    active = true;
    audio_pipeline_send_end_recording();
    ui_show_processing_screen();
    audio_pipeline_start_processing();
}

void mode_todo_stop(void)
{
    audio_pipeline_stop_processing();
    active = false;
    ui_show_home_screen();
}

void mode_todo_handle_response(const char *text)
{
    if (!active) return;
    audio_pipeline_stop_processing();
    ui_show_response(text);
}
