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

    i2s_chan_handle_t tx = audio_get_tx_handle();
    if (!tx) {
        ESP_LOGE(TAG, "Playback aborted: no I2S TX handle");
        free(path);
        vTaskDelete(NULL);
        return;
    }

    board_power_audio_on();
    es8311_set_volume(100);
    audio_pipeline_set_playback_active(true);

    int reg31 = 0, reg32 = 0, reg0c = 0;
    esp_codec_dev_read_reg(es8311_get_handle(), 0x31, &reg31);
    esp_codec_dev_read_reg(es8311_get_handle(), 0x32, &reg32);
    esp_codec_dev_read_reg(es8311_get_handle(), 0x0C, &reg0c);
    ESP_LOGI(TAG, "Playback regs: 0x0C=0x%02X 0x31=0x%02X 0x32=0x%02X", reg0c, reg31, reg32);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        audio_pipeline_set_playback_active(false);
        free(path);
        vTaskDelete(NULL);
        return;
    }

    is_playing = true;
    int16_t *mono_buf = heap_caps_malloc(256 * sizeof(int16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    int16_t *stereo_buf = heap_caps_malloc(512 * sizeof(int16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!mono_buf || !stereo_buf) {
        ESP_LOGE(TAG, "Playback DMA buffer alloc failed");
        free(mono_buf);
        free(stereo_buf);
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

        size_t read = fread(mono_buf, sizeof(int16_t), 256, f);
        if (read == 0) break;

        for (size_t i = 0; i < read; i++) {
            stereo_buf[2*i] = mono_buf[i];
            stereo_buf[2*i + 1] = mono_buf[i];
        }

        size_t bytes_to_write = read * 2 * sizeof(int16_t);
        size_t written = 0;
        esp_err_t ret = i2s_channel_write(tx, stereo_buf, bytes_to_write, &written, pdMS_TO_TICKS(200));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
            break;
        }
        if (written < bytes_to_write) {
            ESP_LOGW(TAG, "I2S short write: %u/%u", (unsigned)written, (unsigned)bytes_to_write);
        }
    }

    heap_caps_free(mono_buf);
    heap_caps_free(stereo_buf);
    fclose(f);
    is_playing = false;
    audio_pipeline_set_playback_active(false);
    ESP_LOGI(TAG, "Playback finished: %s", path);
    free(path);
    vTaskDelete(NULL);
}

void speaker_set_handle(void *handle)
{
}

void speaker_init(void)
{
    ESP_LOGI(TAG, "Speaker ready (esp_codec_dev, %d Hz)", AUDIO_SAMPLE_RATE);
}

esp_err_t speaker_enable(void)
{
    return ESP_OK;
}

esp_err_t speaker_play(const int16_t *audio, size_t samples)
{
    i2s_chan_handle_t tx = audio_get_tx_handle();
    if (!tx) return ESP_ERR_INVALID_STATE;

    is_playing = true;

    int16_t *stereo = heap_caps_malloc(samples * 2 * sizeof(int16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!stereo) {
        is_playing = false;
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < samples; i++) {
        int16_t s = (volume < 100) ? (audio[i] * volume) / 100 : audio[i];
        stereo[2*i] = s;
        stereo[2*i + 1] = s;
    }

    size_t bytes_to_write = samples * 2 * sizeof(int16_t);
    size_t written = 0;
    esp_err_t ret = i2s_channel_write(tx, stereo, bytes_to_write, &written, pdMS_TO_TICKS(200));

    heap_caps_free(stereo);
    is_playing = false;

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "speaker_play I2S write failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    return ESP_OK;
}

void speaker_play_file(const char *pcm_path)
{
    if (!pcm_path || !es8311_get_handle()) return;

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
}

esp_err_t speaker_play_tone(int freq_hz, int duration_ms)
{
    i2s_chan_handle_t tx = audio_get_tx_handle();
    if (!tx) return ESP_ERR_INVALID_STATE;

    board_power_audio_on();
    es8311_set_volume(100);
    audio_pipeline_set_playback_active(true);

    uint8_t r = 0;
    ESP_LOGI(TAG, "GPIO3 (AMP_EN) level=%d", gpio_get_level(GPIO_NUM_3));
    ESP_LOGI(TAG, "ES8311 regs: 0x0C=0x%02X 0x31=0x%02X 0x32=0x%02X",
             es8311_read_reg(0x0C, &r)?0:r, es8311_read_reg(0x31, &r)?0:r, es8311_read_reg(0x32, &r)?0:r);

    int sample_rate = AUDIO_SAMPLE_RATE;
    int total_samples = (sample_rate * duration_ms) / 1000;

    int16_t *buf = heap_caps_malloc(total_samples * 2 * sizeof(int16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf) {
        audio_pipeline_set_playback_active(false);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < total_samples; i++) {
        int16_t s = (int16_t)((int32_t)12000 * sin(2 * 3.14159 * freq_hz * i / sample_rate));
        buf[2*i] = s;
        buf[2*i + 1] = s;
    }

    size_t written = 0;
    esp_err_t ret = i2s_channel_write(tx, buf, total_samples * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(duration_ms + 100));

    heap_caps_free(buf);
    audio_pipeline_set_playback_active(false);
    ESP_LOGI(TAG, "Tone %d Hz %d ms: ret=%s written=%u/%u", freq_hz, duration_ms,
             esp_err_to_name(ret), (unsigned)written, (unsigned)(total_samples * 2 * sizeof(int16_t)));
    return ret;
}
