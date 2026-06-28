#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_codec_dev.h"
#include "app_config.h"
#include "es8311.h"
#include "audio_pipeline.h"
#include "system_init.h"

static const char *TAG = "SPEAKER";

static bool is_playing = false;
static uint8_t volume = 80;
static bool playback_stop = false;

static void playback_task(void *arg)
{
    char *path = (char *)arg;
    ESP_LOGI(TAG, "Playback task started: %s", path);

    esp_codec_dev_handle_t h = es8311_get_playback_handle();
    if (!h) {
        ESP_LOGE(TAG, "Playback aborted: no codec handle");
        free(path);
        vTaskDelete(NULL);
        return;
    }

    board_power_audio_on();
    esp_codec_dev_set_out_vol(h, 100);
    audio_pipeline_set_playback_active(true);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        audio_pipeline_set_playback_active(false);
        free(path);
        vTaskDelete(NULL);
        return;
    }

    is_playing = true;
    int16_t *buf = heap_caps_malloc(256 * sizeof(int16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf) {
        ESP_LOGE(TAG, "Playback buffer alloc failed");
        fclose(f);
        is_playing = false;
        audio_pipeline_set_playback_active(false);
        free(path);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        if (playback_stop) {
            ESP_LOGI(TAG, "Playback stopped");
            break;
        }
        size_t read = fread(buf, sizeof(int16_t), 256, f);
        if (read == 0) break;
        int ret = esp_codec_dev_write(h, buf, read * sizeof(int16_t));
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "esp_codec_dev_write failed: %d", ret);
            break;
        }
    }

    heap_caps_free(buf);
    fclose(f);
    is_playing = false;
    audio_pipeline_set_playback_active(false);
    ESP_LOGI(TAG, "Playback finished: %s", path);
    free(path);
    vTaskDelete(NULL);
}

esp_err_t speaker_play(const int16_t *audio, size_t samples)
{
    esp_codec_dev_handle_t h = es8311_get_playback_handle();
    if (!h) return ESP_ERR_INVALID_STATE;

    is_playing = true;
    int ret = esp_codec_dev_write(h, (void *)audio, samples * sizeof(int16_t));
    is_playing = false;

    return (ret == ESP_CODEC_DEV_OK) ? ESP_OK : ESP_FAIL;
}

void speaker_play_file(const char *pcm_path)
{
    if (!pcm_path) return;

    if (is_playing) {
        playback_stop = true;
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    playback_stop = false;

    char *path_copy = strdup(pcm_path);
    if (!path_copy) {
        ESP_LOGE(TAG, "Failed to allocate path for playback");
        return;
    }

    BaseType_t ret = xTaskCreate(playback_task, "spk_playback", 4096, path_copy, 4, NULL);
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create playback task");
        free(path_copy);
    }
}

void speaker_stop(void)
{
    playback_stop = true;
}

void speaker_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    volume = vol;
    esp_codec_dev_handle_t h = es8311_get_playback_handle();
    if (h) esp_codec_dev_set_out_vol(h, vol);
}

esp_err_t speaker_play_tone(int freq_hz, int duration_ms)
{
    esp_codec_dev_handle_t h = es8311_get_playback_handle();
    if (!h) return ESP_ERR_INVALID_STATE;

    board_power_audio_on();
    esp_codec_dev_set_out_vol(h, 100);
    audio_pipeline_set_playback_active(true);

    int sample_rate = AUDIO_SAMPLE_RATE;
    int total_samples = (sample_rate * duration_ms) / 1000;

    int16_t *buf = heap_caps_malloc(total_samples * sizeof(int16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf) {
        audio_pipeline_set_playback_active(false);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < total_samples; i++) {
        buf[i] = (int16_t)((int32_t)12000 * sin(2 * 3.14159 * freq_hz * i / sample_rate));
    }

    int ret = esp_codec_dev_write(h, buf, total_samples * sizeof(int16_t));
    heap_caps_free(buf);
    audio_pipeline_set_playback_active(false);
    ESP_LOGI(TAG, "Tone %d Hz %d ms: ret=%d", freq_hz, duration_ms, ret);
    return (ret == ESP_CODEC_DEV_OK) ? ESP_OK : ESP_FAIL;
}
