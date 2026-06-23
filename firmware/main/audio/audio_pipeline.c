#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2s_std.h"
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
#include "recordings.h"
#include "ui_manager.h"
#include "es8311.h"

static const char *TAG = "AUDIO_PIPELINE";

static ringbuffer_t audio_rb;
static bool recording = false;
static bool processing = false;
static audio_mode_t current_mode = MODE_AGENT;
static bool offline_recording = false;
static bool pipeline_docked = false;

// Playback test buffer — accumulates audio during recording for speaker loopback
#define PLAYBACK_BUF_MAX_SAMPLES  (AUDIO_SAMPLE_RATE * 3)
static int16_t *playback_buf = NULL;
static size_t playback_len = 0;

static EventGroupHandle_t audio_events;
#define AUDIO_EVENT_VAD_TRIGGERED  BIT0
#define AUDIO_EVENT_WAKE_WORD      BIT1

#define VAD_BURST_MS        50
#define VAD_BURST_SAMPLES   ((AUDIO_SAMPLE_RATE * VAD_BURST_MS) / 1000)
#define VAD_CONFIRM_FRAMES  4

#define UI_UPDATE_INTERVAL_US  (300 * 1000)

static void audio_capture_task(void *arg)
{
    (void)arg;
    int16_t buf[VAD_BURST_SAMPLES];
    int64_t last_ui = 0;

    while (1) {
        if (recording) {
            size_t read = 0;
            esp_err_t ret = mic_read(buf, VAD_BURST_SAMPLES, &read);
            if (ret == ESP_OK && read > 0) {
                if (offline_recording) {
                    recording_write_audio(buf, read);
                } else {
                    if (ringbuffer_available(&audio_rb) + read <= audio_rb.size) {
                        ringbuffer_write(&audio_rb, buf, read);
                    }
                    if (playback_buf) {
                        size_t room = PLAYBACK_BUF_MAX_SAMPLES - playback_len;
                        size_t copy = read < room ? read : room;
                        if (copy > 0) {
                            memcpy(playback_buf + playback_len, buf, copy * sizeof(int16_t));
                            playback_len += copy;
                        }
                    }
                    const char *mode_str[] = {"agent", "note", "transcribe", "todo"};
                    ws_client_send_audio_mode((uint8_t *)buf, read * sizeof(int16_t),
                                              mode_str[current_mode]);
                }

                int64_t now = esp_timer_get_time();
                if (now - last_ui > UI_UPDATE_INTERVAL_US) {
                    last_ui = now;
                    int32_t energy = 0;
                    for (size_t i = 0; i < read; i++) energy += abs(buf[i]);
                    energy /= (int32_t)read;
                    ui_update_recording_viz(energy);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
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
        if (recording || pipeline_docked) {
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
        if (pipeline_docked) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

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

static void audio_i2s_duplex_init(void)
{
    i2s_chan_config_t chan_cfg = {
        .id = I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = 512,
        .auto_clear = true,
    };

    i2s_chan_handle_t tx = NULL, rx = NULL;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx, &rx));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = 16,
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .bit_shift = true,
        },
        .gpio_cfg = {
            .mclk = I2S_MCLK_GPIO,
            .bclk = I2S_BCLK_GPIO,
            .ws = I2S_WS_GPIO,
            .dout = I2S_DOUT_GPIO,
            .din = I2S_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std_cfg));
    speaker_set_handle(tx);
    mic_set_handle(rx);

    ESP_LOGI(TAG, "I2S duplex initialized (%d Hz, mono)", AUDIO_SAMPLE_RATE);
}

void audio_pipeline_init(void)
{
    ringbuffer_init(&audio_rb, AUDIO_BUFFER_SIZE * 4);
    audio_i2s_duplex_init();
    ESP_ERROR_CHECK(speaker_enable());       // start TX clock (MCLK) before ES8311 init
    ESP_ERROR_CHECK(es8311_init());
    wake_word_init();
    vad_init();

    audio_events = xEventGroupCreate();

    xTaskCreate(audio_capture_task, "audio_capture", 4096, NULL, 5, NULL);
    xTaskCreate(anim_task, "anim", 2048, NULL, 3, NULL);
    xTaskCreate(vad_task, "vad", 3072, NULL, 4, NULL);
    xTaskCreate(wake_word_task, "wake_word", 4096, NULL, 3, NULL);
    playback_buf = (int16_t *)calloc(PLAYBACK_BUF_MAX_SAMPLES, sizeof(int16_t));
    if (!playback_buf) {
        ESP_LOGE(TAG, "Failed to allocate playback buffer (%zu bytes)",
                 PLAYBACK_BUF_MAX_SAMPLES * sizeof(int16_t));
    }
    mic_start();
    ESP_LOGI(TAG, "Audio pipeline initialized (two-stage VAD + wake word)");
}

void audio_pipeline_start_recording(audio_mode_t mode)
{
    if (recording) return;
    current_mode = mode;
    recording = true;
    processing = false;
    power_mark_activity();
    ESP_LOGI(TAG, "Recording started (mode=%d)", mode);
}

struct playback_arg {
    int16_t *buf;
    size_t len;
};

static void playback_task(void *arg)
{
    struct playback_arg *pa = (struct playback_arg *)arg;
    speaker_play(pa->buf, pa->len);
    free(pa->buf);
    free(pa);
    vTaskDelete(NULL);
}

void audio_pipeline_stop_recording(void)
{
    if (!recording) return;
    recording = false;
    vad_reset();
    if (playback_buf && playback_len > 0) {
        int16_t *copy = malloc(playback_len * sizeof(int16_t));
        if (copy) {
            memcpy(copy, playback_buf, playback_len * sizeof(int16_t));
            struct playback_arg *pa = malloc(sizeof(struct playback_arg));
            if (pa) {
                pa->buf = copy;
                pa->len = playback_len;
                xTaskCreate(playback_task, "playback", 4096, pa, 5, NULL);
            } else {
                free(copy);
            }
        }
        playback_len = 0;
    }
    ESP_LOGI(TAG, "Recording stopped");
}

void audio_pipeline_start_processing(void)
{
    processing = true;
}

void audio_pipeline_stop_processing(void)
{
    processing = false;
}

void audio_pipeline_send_end_recording(void)
{
    const char *mode_str[] = {"agent", "note", "transcribe", "todo"};
    char msg[128];
    snprintf(msg, sizeof(msg),
             "{\"type\":\"end\",\"mode\":\"%s\",\"session_id\":\"\"}",
             mode_str[current_mode]);
    ws_client_send_json(msg);
    ESP_LOGI(TAG, "End of recording sent (mode=%s)", mode_str[current_mode]);
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
             type == REC_TYPE_NOTE ? "NOTE" : "TODO");
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
