#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "app_config.h"
#include "mic_driver.h"
#include "speaker_driver.h"
#include "wake_word.h"
#include "vad.h"
#include "ws_client.h"
#include "power_mgmt.h"
#include "ringbuffer.h"
#include "audio_pipeline.h"

static const char *TAG = "AUDIO_PIPELINE";

static ringbuffer_t audio_rb;
static bool recording = false;
static audio_mode_t current_mode = MODE_AGENT;

static EventGroupHandle_t audio_events;
#define AUDIO_EVENT_VAD_TRIGGERED  BIT0
#define AUDIO_EVENT_WAKE_WORD      BIT1

#define VAD_BURST_MS        50
#define VAD_BURST_SAMPLES   ((AUDIO_SAMPLE_RATE * VAD_BURST_MS) / 1000)
#define VAD_CONFIRM_FRAMES  4

static void audio_capture_task(void *arg)
{
    (void)arg;
    int16_t buf[VAD_BURST_SAMPLES];

    while (1) {
        if (recording) {
            size_t read = 0;
            esp_err_t ret = mic_read(buf, VAD_BURST_SAMPLES, &read);
            if (ret == ESP_OK && read > 0) {
                if (ringbuffer_available(&audio_rb) + read <= audio_rb.size) {
                    ringbuffer_write(&audio_rb, buf, read);
                }
                ws_client_send_audio((uint8_t *)buf, read * sizeof(int16_t));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void vad_task(void *arg)
{
    (void)arg;
    int16_t buf[VAD_BURST_SAMPLES];
    int confirm_count = 0;
    bool in_vad_trigger = false;

    while (1) {
        if (recording) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        size_t read = 0;
        esp_err_t ret = mic_read(buf, VAD_BURST_SAMPLES, &read);
        if (ret != ESP_OK || read == 0) {
            vTaskDelay(pdMS_TO_TICKS(VAD_BURST_MS));
            continue;
        }

        int32_t energy = vad_compute_energy(buf, read);
        ESP_LOGD(TAG, "VAD energy: %ld", (long)energy);

        if (energy >= AUDIO_VAD_THRESHOLD) {
            confirm_count++;
            if (confirm_count >= VAD_CONFIRM_FRAMES && !in_vad_trigger) {
                ESP_LOGI(TAG, "VAD triggered (energy=%ld)", (long)energy);
                in_vad_trigger = true;
                power_mark_activity();
                xEventGroupSetBits(audio_events, AUDIO_EVENT_VAD_TRIGGERED);
            }
        } else {
            confirm_count = 0;
            if (in_vad_trigger) {
                int64_t elapsed = 0;
                (void)elapsed;
                in_vad_trigger = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(VAD_BURST_MS / 2));
    }
}

static void wake_word_task(void *arg)
{
    (void)arg;
    EventBits_t bits;
    int16_t buf[VAD_BURST_SAMPLES];
    int wake_word_checks = 0;

    while (1) {
        bits = xEventGroupWaitBits(audio_events, AUDIO_EVENT_VAD_TRIGGERED,
                                   pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & AUDIO_EVENT_VAD_TRIGGERED) {
            ESP_LOGI(TAG, "Wake word checking started (VAD triggered)");
            wake_word_checks = 0;

            while (wake_word_checks < AUDIO_MAX_WAKE_WORD_CHECKS) {
                size_t read = 0;
                esp_err_t ret = mic_read(buf, VAD_BURST_SAMPLES, &read);

                if (!recording && ret == ESP_OK && read > 0) {
                    if (wake_word_detect(buf, read)) {
                        ESP_LOGI(TAG, "Wake word detected!");
                        xEventGroupSetBits(audio_events, AUDIO_EVENT_WAKE_WORD);
                        audio_pipeline_start_recording(MODE_AGENT);
                        break;
                    }
                    wake_word_checks++;

                    int32_t energy = vad_compute_energy(buf, read);
                    if (energy < AUDIO_VAD_THRESHOLD / 2) {
                        ESP_LOGI(TAG, "Voice stopped, wake word not detected");
                        break;
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(VAD_BURST_MS / 2));
            }

            if (!recording) {
                ESP_LOGI(TAG, "No wake word detected, returning to sleep");
            }
        }
    }
}

void audio_pipeline_init(void)
{
    ringbuffer_init(&audio_rb, AUDIO_BUFFER_SIZE * 4);
    mic_init();
    speaker_init();
    wake_word_init();
    vad_init();

    audio_events = xEventGroupCreate();

    xTaskCreate(audio_capture_task, "audio_capture", 4096, NULL, 5, NULL);
    xTaskCreate(vad_task, "vad", 3072, NULL, 4, NULL);
    xTaskCreate(wake_word_task, "wake_word", 4096, NULL, 3, NULL);

    mic_start();
    ESP_LOGI(TAG, "Audio pipeline initialized (two-stage VAD + wake word)");
}

void audio_pipeline_start_recording(audio_mode_t mode)
{
    if (recording) return;
    current_mode = mode;
    recording = true;
    power_mark_activity();
    ESP_LOGI(TAG, "Recording started (mode=%d)", mode);
}

void audio_pipeline_stop_recording(void)
{
    if (!recording) return;
    recording = false;
    vad_reset();
    ESP_LOGI(TAG, "Recording stopped");
}

void audio_pipeline_play_tts(const uint8_t *audio, size_t len)
{
    size_t samples = len / sizeof(int16_t);
    speaker_play((const int16_t *)audio, samples);
}

bool audio_pipeline_is_recording(void)
{
    return recording;
}
