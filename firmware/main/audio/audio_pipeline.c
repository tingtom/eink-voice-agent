#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "app_config.h"
#include "mic_driver.h"
#include "speaker_driver.h"
#include "wake_word.h"
#include "vad.h"
#include "power_mgmt.h"
#include "ringbuffer.h"
#include "audio_pipeline.h"
#include "recordings.h"
#include "ui_manager.h"
#include "es8311.h"
#include "esp_heap_caps.h"
#include "system_init.h"
#include "wifi_manager.h"
#include "driver/i2s_std.h"

static const char *TAG = "AUDIO_PIPELINE";

static ringbuffer_t audio_rb;
static bool recording = false;
static bool processing = false;
static bool playback_active = false;
static audio_mode_t current_mode = MODE_AGENT;
static bool offline_recording = false;
static bool pipeline_docked = false;
static bool wake_word_checking = false;

static audio_ui_cb_t wake_failed_cb = NULL;
static audio_ui_cb_t wake_detected_cb = NULL;
static audio_ui_cb_t recording_ended_cb = NULL;
static response_cb_t response_cb = NULL;
static char cached_response[512] = "";

static EventGroupHandle_t audio_events;
#define AUDIO_EVENT_VAD_TRIGGERED  BIT0
#define AUDIO_EVENT_WAKE_WORD      BIT1
#define AUDIO_EVENT_WS_CONNECTED   BIT2

#define VAD_BURST_MS        50
#define VAD_BURST_SAMPLES   ((AUDIO_SAMPLE_RATE * VAD_BURST_MS) / 1000)
#define VAD_CONFIRM_FRAMES  2
#define WS_SEND_CHUNK_SAMPLES (VAD_BURST_SAMPLES)

#define UI_UPDATE_INTERVAL_US  (300 * 1000)

static uint32_t send_packets = 0;
static uint32_t send_failures = 0;
static uint32_t rb_empty_count = 0;

static void audio_capture_task(void *arg)
{
    (void)arg;
    int16_t buf[VAD_BURST_SAMPLES];
    int64_t last_ui = 0;
    uint32_t iter = 0;
    int64_t last_iter_time = 0;

    while (1) {
        if (pipeline_docked || wake_word_checking || processing) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (!recording) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t read = 0;
        esp_err_t ret = mic_read(buf, VAD_BURST_SAMPLES, &read);
        int64_t now = esp_timer_get_time();
        int32_t delta_us = (int32_t)(now - last_iter_time);
        last_iter_time = now;
        iter++;
        if (iter % 5 == 0) {
            ESP_LOGI(TAG, "capture iter=%lu read=%u ret=%s delta_us=%ld heap=%u avail=%u",
                     (unsigned long)iter, (unsigned)read,
                     ret == ESP_OK ? "OK" : ret == ESP_ERR_TIMEOUT ? "TIMEOUT" : "OTHER",
                     (long)delta_us, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)ringbuffer_available(&audio_rb));
        }

        if (ret == ESP_OK && read > 0) {
            if (offline_recording) {
                recording_write_audio(buf, read);
            } else {
                if (!ringbuffer_write(&audio_rb, buf, read)) {
                    ESP_LOGW(TAG, "ringbuffer full, dropped %u samples (avail=%u)",
                             (unsigned)read, (unsigned)ringbuffer_available(&audio_rb));
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
        } else if (ret != ESP_OK) {
            ESP_LOGE(TAG, "mic_read failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGW(TAG, "mic_read returned 0 (iter=%lu)", (unsigned long)iter);
        }

        if (!offline_recording) {
            now = esp_timer_get_time();
            if (now - last_ui > UI_UPDATE_INTERVAL_US) {
                last_ui = now;
                int32_t energy = 0;
                for (size_t i = 0; i < read; i++) energy += abs(buf[i]);
                energy /= (int32_t)read;
                ui_update_recording_viz(energy);
            }
        }

        if (ret != ESP_OK || read == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

static void http_send_task(void *arg)
{
    (void)arg;
    int16_t send_buf[WS_SEND_CHUNK_SAMPLES];
    const size_t chunk_samples = sizeof(send_buf) / sizeof(send_buf[0]);

    while (1) {
        if (!recording || pipeline_docked || !wifi_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        bool success = ringbuffer_read(&audio_rb, send_buf, chunk_samples);
        if (!success) {
            rb_empty_count++;
            if (rb_empty_count % 100 == 0) {
                ESP_LOGW(TAG, "http_send: ringbuffer empty (count=%u, failures=%u, sent=%u)",
                         (unsigned)ringbuffer_available(&audio_rb), (unsigned)rb_empty_count, (unsigned)send_packets);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        char url[256];
        snprintf(url, sizeof(url), "%s/api/device/audio?mode=%s",
                 HERMES_HTTP_URL,
                 current_mode == MODE_AGENT ? "agent" :
                 current_mode == MODE_NOTE ? "note" :
                 current_mode == MODE_TRANSCRIBE ? "transcribe" : "todo");

        char resp[64];
        bool posted = false;
        for (int attempt = 0; attempt < 3; attempt++) {
            if (!wifi_is_connected()) {
                ESP_LOGW(TAG, "http_audio: WiFi disconnected, attempt %d/3", attempt + 1);
                vTaskDelay(pdMS_TO_TICKS(500 * (attempt + 1)));
                continue;
            }
            esp_err_t ret = http_post_binary(url, (const uint8_t *)send_buf, chunk_samples * sizeof(int16_t), resp, sizeof(resp));
            if (ret == ESP_OK) {
                posted = true;
                send_packets++;
                break;
            }
            ESP_LOGW(TAG, "http_audio: POST attempt %d/3 failed: %s", attempt + 1, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(200 * (attempt + 1)));
        }
        if (!posted) {
            send_failures++;
            ESP_LOGE(TAG, "http_audio: all retries failed for %s len=%u", url, (unsigned)(chunk_samples * sizeof(int16_t)));
        }
    }
}

static void anim_task(void *arg)
{
    (void)arg;
    int frame = 0;

    while (1) {
        if (recording) {
            vTaskDelay(pdMS_TO_TICKS(300));
        } else if (processing) {
            ui_update_processing_anim(frame++);
            vTaskDelay(pdMS_TO_TICKS(250));
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

static void vad_task(void *arg)
{
    (void)arg;
    int16_t buf[VAD_BURST_SAMPLES];
    int confirm_count = 0;
    bool in_vad_trigger = false;

    while (1) {
        if (recording || pipeline_docked || wake_word_checking || processing || playback_active) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        size_t read = 0;
        esp_err_t ret = mic_read(buf, VAD_BURST_SAMPLES, &read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "mic_read failed: %s (confirm=%d)", esp_err_to_name(ret), confirm_count);
            vTaskDelay(pdMS_TO_TICKS(VAD_BURST_MS));
            continue;
        }
        if (read == 0) {
            ESP_LOGW(TAG, "mic_read returned 0 samples");
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
        if (pipeline_docked || recording || processing || playback_active) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        bits = xEventGroupWaitBits(audio_events, AUDIO_EVENT_VAD_TRIGGERED,
                                   pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & AUDIO_EVENT_VAD_TRIGGERED) {
            ESP_LOGI(TAG, "Wake word checking started (VAD triggered)");
            wake_word_checking = true;
            wake_word_reset();
            wake_word_checks = 0;

            while (wake_word_checks < AUDIO_MAX_WAKE_WORD_CHECKS) {
                if (recording || playback_active) break;
                size_t read = 0;
                esp_err_t ret = mic_read(buf, VAD_BURST_SAMPLES, &read);
                if (ret == ESP_OK && read > 0) {
                    if (wake_word_detect(buf, read)) {
                        ESP_LOGI(TAG, "Wake word detected!");
                        xEventGroupSetBits(audio_events, AUDIO_EVENT_WAKE_WORD);
                        audio_pipeline_start_recording(MODE_AGENT);
                        if (wake_detected_cb) wake_detected_cb();
                        break;
                    }
                    wake_word_checks++;
                } else if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "wake_word mic_read failed: %s", esp_err_to_name(ret));
                } else {
                    ESP_LOGD(TAG, "wake_word mic_read returned 0");
                }
                vTaskDelay(pdMS_TO_TICKS(VAD_BURST_MS / 2));
            }

            wake_word_checking = false;
            if (!recording) {
                ESP_LOGI(TAG, "No wake word detected, returning to sleep");
                if (wake_failed_cb) wake_failed_cb();
            }
        }
    }
}

void audio_pipeline_init(void)
{
    ringbuffer_init(&audio_rb, AUDIO_BUFFER_SIZE);
    esp_err_t ret = es8311_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 init failed: %s", esp_err_to_name(ret));
    }
    wake_word_init();
    vad_init();

    audio_events = xEventGroupCreate();

    xTaskCreate(audio_capture_task, "audio_capture", 4096, NULL, 5, NULL);
    xTaskCreate(http_send_task, "http_send", 8192, NULL, 3, NULL);
    xTaskCreate(anim_task, "anim", 2048, NULL, 3, NULL);
    xTaskCreate(vad_task, "vad", 4096, NULL, 4, NULL);
    xTaskCreate(wake_word_task, "wake_word", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Audio pipeline initialized (two-stage VAD + wake word)");
}

void audio_pipeline_start_recording(audio_mode_t mode)
{
    if (recording) return;
    current_mode = mode;
    recording = true;
    processing = false;
    power_mark_activity();
    wake_word_checking = false;
    ringbuffer_clear(&audio_rb);
    const char *mode_str[] = {"agent", "note", "transcribe", "todo"};
    ESP_LOGI(TAG, "Recording started (mode=%s)", mode_str[mode]);
}

void audio_pipeline_stop_recording(void)
{
    if (!recording) return;
    recording = false;
    vad_reset();
    if (recording_ended_cb) recording_ended_cb();
    ESP_LOGI(TAG, "Recording stopped (total_send_packets=%lu)", (unsigned long)send_packets);
}

void audio_pipeline_start_processing(void)
{
    processing = true;
}

void audio_pipeline_stop_processing(void)
{
    processing = false;
}

// Extract string value from JSON: search for "\"key\":\" (with optional space after colon)
// Returns pointer to value text (null-terminated in-place) or NULL if not found.
static const char *json_extract_string(const char *json, const char *key)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *start = strstr(json, pattern);
    if (!start) {
        snprintf(pattern, sizeof(pattern), "\"%s\": \"", key);
        start = strstr(json, pattern);
        if (!start) return NULL;
    }
    start += strlen(pattern);
    const char *end = strchr(start, '"');
    if (!end) return NULL;
    char *val = (char *)start;
    val[end - start] = '\0';
    return start;
}

static void response_poll_task(void *arg)
{
    (void)arg;
    char url[192];
    snprintf(url, sizeof(url), "%s/api/device/response?chat_id=eink:%s",
             HERMES_HTTP_URL, DEVICE_ID);

    char resp[1024];
    int retries = 0;
    const int max_retries = 13;

    vTaskDelay(pdMS_TO_TICKS(500));

    if (cached_response[0] != '\0') {
        ESP_LOGI(TAG, "Response from cache: %.100s", cached_response);
        if (response_cb) response_cb(cached_response);
        cached_response[0] = '\0';
        goto done;
    }

    while (retries < max_retries) {
        if (!processing) {
            ESP_LOGI(TAG, "Response poll cancelled");
            goto done;
        }

        resp[0] = '\0';
        esp_err_t ret = http_get_json(url, resp, sizeof(resp) - 1);
        if (ret != ESP_OK) {
            retries++;
            vTaskDelay(pdMS_TO_TICKS(1500));
            continue;
        }
        resp[sizeof(resp) - 1] = '\0';

        const char *type = json_extract_string(resp, "type");
        if (type && strcmp(type, "response") == 0) {
            const char *data = json_extract_string(resp, "data");
            if (data) {
                ESP_LOGI(TAG, "Response received: %.100s", data);
                if (response_cb) response_cb(data);
                goto done;
            }
        }

        retries++;
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    ESP_LOGW(TAG, "Response poll timed out");
    if (response_cb) response_cb("Response timed out");

done:
    vTaskDelete(NULL);
}

void audio_pipeline_send_end_recording(void)
{
    const char *mode_str[] = {"agent", "note", "transcribe", "todo"};
    char url[256];
    snprintf(url, sizeof(url), "%s/api/device/audio/end", HERMES_HTTP_URL);

    char json[128];
    snprintf(json, sizeof(json), "{\"mode\":\"%s\"}", mode_str[current_mode]);
    char resp[64];
    esp_err_t ret = http_post_json(url, json, resp, sizeof(resp));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "http_audio: end POST failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "End of recording sent (mode=%s)", mode_str[current_mode]);

    resp[sizeof(resp) - 1] = '\0';
    cached_response[0] = '\0';
    const char *data = json_extract_string(resp, "data");
    if (data && !strstr(data, "Audio received") && !strstr(data, "command processed")) {
        strlcpy(cached_response, data, sizeof(cached_response));
        ESP_LOGI(TAG, "Cached end response (%.100s)", cached_response);
    }

    xTaskCreate(response_poll_task, "resp_poll", 4096, NULL, 3, NULL);
}

audio_mode_t audio_pipeline_get_current_mode(void)
{
    return current_mode;
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

bool audio_pipeline_start_offline_recording(rec_type_t type)
{
    if (recording) return false;
    if (!recording_start(type)) return false;
    recording = true;
    offline_recording = true;
    processing = false;
    power_mark_activity();
    ESP_LOGI(TAG, "Offline recording started (type=%s)",
             type == REC_TYPE_NOTE ? "NOTE" :
             type == REC_TYPE_TODO ? "TODO" :
             type == REC_TYPE_AGENT ? "AGENT" : "TRANSCRIBE");
    return true;
}

void audio_pipeline_stop_offline_recording(void)
{
    if (!recording || !offline_recording) return;
    recording = false;
    offline_recording = false;
    recording_stop();
    ESP_LOGI(TAG, "Offline recording stopped");
}

bool audio_pipeline_is_offline_recording(void)
{
    return offline_recording;
}

void audio_pipeline_set_docked(bool docked)
{
    pipeline_docked = docked;
    ESP_LOGI(TAG, "Docked mode %s", docked ? "enabled" : "disabled");
}

bool audio_pipeline_is_docked(void)
{
    return pipeline_docked;
}

void audio_pipeline_set_playback_active(bool active)
{
    playback_active = active;
}

void audio_pipeline_set_wake_failed_cb(audio_ui_cb_t cb)
{
    wake_failed_cb = cb;
}

void audio_pipeline_set_wake_detected_cb(audio_ui_cb_t cb)
{
    wake_detected_cb = cb;
}

void audio_pipeline_set_recording_ended_cb(audio_ui_cb_t cb)
{
    recording_ended_cb = cb;
}

void audio_pipeline_set_response_cb(response_cb_t cb)
{
    response_cb = cb;
}
