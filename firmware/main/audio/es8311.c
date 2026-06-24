#include <string.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "app_config.h"

static const char *TAG = "ES8311";

static const audio_codec_if_t *codec_if = NULL;

void *get_i2c_bus_handle(void);

esp_err_t es8311_init(void)
{
    i2c_master_bus_handle_t bus_handle = (i2c_master_bus_handle_t)get_i2c_bus_handle();
    if (!bus_handle) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = (void *)bus_handle,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl_if) {
        ESP_LOGE(TAG, "Failed to create I2C control interface");
        return ESP_FAIL;
    }

    es8311_codec_cfg_t es8311_cfg = {0};
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8311_cfg.ctrl_if = ctrl_if;
    es8311_cfg.pa_pin = -1;
    es8311_cfg.use_mclk = true;

    codec_if = es8311_codec_new(&es8311_cfg);
    if (!codec_if) {
        ESP_LOGE(TAG, "Failed to create ES8311 codec");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 1,
        .bits_per_sample = 16,
    };

    int ret = codec_if->set_fs(codec_if, &fs);
    if (ret != 0) {
        ESP_LOGE(TAG, "set_fs failed: %d", ret);
        return ESP_FAIL;
    }

    ret = codec_if->enable(codec_if, true);
    if (ret != 0) {
        ESP_LOGE(TAG, "enable failed: %d", ret);
        return ESP_FAIL;
    }

    ret = codec_if->set_mic_gain(codec_if, 40.0);
    if (ret != 0) {
        ESP_LOGE(TAG, "set_mic_gain failed: %d", ret);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ES8311 initialized (16 kHz, 16-bit, I2S slave)");
    return ESP_OK;
}

void es8311_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    if (!codec_if) return;

    // Map 0-100 to dB: compensate for hw_gain (pa_gain) so actual output
    // spans -95.5 dB (mute) to +32.0 dB (max)
    float db = -89.5f + (vol / 100.0f) * 127.5f;
    codec_if->set_vol(codec_if, db);
}
