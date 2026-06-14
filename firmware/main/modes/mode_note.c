#include <string.h>
#include <stdbool.h>
#include "mode_note.h"
#include "esp_log.h"
#include "audio_pipeline.h"
#include "ui_manager.h"

static const char *TAG = "MODE_NOTE";
static bool active = false;

void mode_note_start(void)
{
    ESP_LOGI(TAG, "Note mode started");
    active = true;
    ui_show_recording_screen();
    audio_pipeline_start_recording(MODE_NOTE);
}

void mode_note_stop(void)
{
    ESP_LOGI(TAG, "Note mode stopped");
    audio_pipeline_stop_recording();
    audio_pipeline_send_end_recording();
    ui_show_processing_screen();
    audio_pipeline_start_processing();
}

void mode_note_finish(void)
{
    if (!active) return;
    audio_pipeline_stop_processing();
    active = false;
    ui_show_home_screen();
}

void mode_note_save(const char *transcription)
{
    if (!active) return;
    ESP_LOGI(TAG, "Saving note: %s", transcription);
    ui_show_response("Note saved!");
}
