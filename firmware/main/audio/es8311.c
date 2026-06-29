#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "system_init.h"

static const char *TAG = "ES8311";

static esp_codec_dev_handle_t g_playback_handle = NULL;
static esp_codec_dev_handle_t g_record_handle = NULL;
static i2s_chan_handle_t g_tx = NULL;
static i2s_chan_handle_t g_rx = NULL;
static const audio_codec_ctrl_if_t *g_ctrl_if = NULL;
static const audio_codec_data_if_t *g_data_if = NULL;
static const audio_codec_if_t *g_codec_if = NULL;
static bool g_es8311_inited = false;
static bool g_i2s_hw_inited = false;  // tracks whether i2s_channel_init_std_mode has been called

// Allocate I2S channel handles + DMA buffers WITHOUT programming the peripheral.
// Safe to call before WiFi init (does not touch shared PLL/clock tree).
static esp_err_t es8311_i2s_alloc(void)
{
    if (g_tx && g_rx) return ESP_OK;

    i2s_chan_config_t chan_cfg = {
        .id = I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 2,
        .dma_frame_num = 128,
        .auto_clear = true,
    };

    i2s_chan_handle_t tx = NULL, rx = NULL;
    esp_err_t ret = i2s_new_channel(&chan_cfg, &tx, &rx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    g_tx = tx;
    g_rx = rx;
    g_i2s_hw_inited = false;
    return ESP_OK;
}

// Program I2S peripheral registers (GPIO matrix, clock tree, format).
// Must be called AFTER WiFi init to avoid breaking WiFi RF.
static esp_err_t es8311_i2s_hw_init(void)
{
    if (g_i2s_hw_inited) return ESP_OK;
    if (!g_tx || !g_rx) {
        ESP_LOGE(TAG, "I2S channels not allocated — call es8311_i2s_alloc first");
        return ESP_ERR_INVALID_STATE;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = 32,
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
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

    esp_err_t ret = i2s_channel_init_std_mode(g_rx, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode(rx) failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_init_std_mode(g_tx, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode(tx) failed: %s", esp_err_to_name(ret));
        i2s_del_channel(g_tx);
        i2s_del_channel(g_rx);
        g_tx = NULL;
        g_rx = NULL;
        return ret;
    }

    g_i2s_hw_inited = true;
    return ESP_OK;
}

static void es8311_i2s_deinit(void)
{
    g_i2s_hw_inited = false;
    if (g_tx) {
        i2s_channel_disable(g_tx);
        i2s_del_channel(g_tx);
        g_tx = NULL;
    }
    if (g_rx) {
        i2s_channel_disable(g_rx);
        i2s_del_channel(g_rx);
        g_rx = NULL;
    }
}

// Pre-allocate I2S DMA buffers AND program peripheral BEFORE WiFi init,
// so DMA descriptors land before WiFi's RX pool. Immediately disable the
// channels so MCLK / BCLK / WS don't produce toggling outputs during
// WiFi RF calibration.  Re-enable happens inside esp_codec_dev_open().
esp_err_t es8311_prealloc_i2s(void)
{
    if (g_tx && g_rx && g_i2s_hw_inited) return ESP_OK;
    if (!get_i2c_bus_handle()) i2c_bus_init();
    // NOTE: audio power is NOT toggled here — doing so would set GPIO1/3 as
    // outputs in the no-TCA path, corrupting the GPIO bank shared with
    // BUTTON_PWR (GPIO2).  board_power_audio_on() is called inside
    // es8311_init() after WiFi init.

    esp_err_t ret = es8311_i2s_alloc();
    if (ret != ESP_OK) return ret;
    ret = es8311_i2s_hw_init();
    if (ret != ESP_OK) return ret;

    // Disable channels so clock outputs stay silent during WiFi init.
    // DMA descriptors persist — they are freed only in i2s_del_channel().
    // Note: i2s_channel_init_std_mode does NOT enable channels, so this is safe.
    // The channels will be enabled when esp_codec_dev_open is called.
    // i2s_channel_disable(g_tx);  // Not needed - channels aren't enabled yet
    // i2s_channel_disable(g_rx);

    return ESP_OK;
}

esp_err_t es8311_init(void)
{
    if (g_es8311_inited) return ESP_OK;

    void *bus = get_i2c_bus_handle();
    if (!bus) {
        ESP_LOGE(TAG, "I2C bus not initialized — call i2c_bus_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    board_power_audio_on();

    esp_err_t ret = ESP_OK;
    if (!g_tx || !g_rx) {
        ret = es8311_i2s_alloc();
    }
    if (ret == ESP_OK && !g_i2s_hw_inited) {
        ret = es8311_i2s_hw_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    // If pre-alloc ran, channels were disabled to keep MCLK silent during
    // WiFi init.  esp_codec_dev_open() (called below) will re-enable them.

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bus,
    };
    g_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!g_ctrl_if) {
        ESP_LOGE(TAG, "Failed to create I2C control interface");
        es8311_i2s_deinit();
        return ESP_FAIL;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_PORT,
        .rx_handle = (void *)g_rx,
        .tx_handle = (void *)g_tx,
        .clk_src = 0,
    };
    g_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!g_data_if) {
        ESP_LOGE(TAG, "Failed to create I2S data interface");
        audio_codec_delete_ctrl_if(g_ctrl_if);
        g_ctrl_if = NULL;
        es8311_i2s_deinit();
        return ESP_FAIL;
    }

    es8311_codec_cfg_t codec_cfg = {
        .ctrl_if = g_ctrl_if,
        .gpio_if = NULL,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = -1,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {
            .pa_voltage = 5.0f,
            .codec_dac_voltage = 3.3f,
            .pa_gain = 0.0f,
        },
        .no_dac_ref = true,
        .mclk_div = 256,
    };
    g_codec_if = es8311_codec_new(&codec_cfg);
    if (!g_codec_if) {
        ESP_LOGE(TAG, "Failed to create ES8311 codec interface");
        audio_codec_delete_data_if(g_data_if);
        g_data_if = NULL;
        audio_codec_delete_ctrl_if(g_ctrl_if);
        g_ctrl_if = NULL;
        es8311_i2s_deinit();
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t out_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = g_codec_if,
        .data_if = g_data_if,
    };
    g_playback_handle = esp_codec_dev_new(&out_cfg);
    if (!g_playback_handle) {
        ESP_LOGE(TAG, "Failed to create playback device");
        audio_codec_delete_codec_if(g_codec_if);
        g_codec_if = NULL;
        audio_codec_delete_data_if(g_data_if);
        g_data_if = NULL;
        audio_codec_delete_ctrl_if(g_ctrl_if);
        g_ctrl_if = NULL;
        es8311_i2s_deinit();
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t in_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = g_codec_if,
        .data_if = g_data_if,
    };
    g_record_handle = esp_codec_dev_new(&in_cfg);
    if (!g_record_handle) {
        ESP_LOGE(TAG, "Failed to create record device");
        esp_codec_dev_delete(g_playback_handle);
        g_playback_handle = NULL;
        audio_codec_delete_codec_if(g_codec_if);
        g_codec_if = NULL;
        audio_codec_delete_data_if(g_data_if);
        g_data_if = NULL;
        audio_codec_delete_ctrl_if(g_ctrl_if);
        g_ctrl_if = NULL;
        es8311_i2s_deinit();
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_SAMPLE_RATE,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0,
        .mclk_multiple = 256,
    };

    int open_ret = esp_codec_dev_open(g_playback_handle, &fs);
    if (open_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open playback device: %d", open_ret);
        goto fail;
    }

    open_ret = esp_codec_dev_open(g_record_handle, &fs);
    if (open_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open record device: %d", open_ret);
        esp_codec_dev_close(g_playback_handle);
        goto fail;
    }

    esp_codec_set_disable_when_closed(g_playback_handle, false);
    esp_codec_set_disable_when_closed(g_record_handle, false);

    // Ensure ADC gain is set — Waveshare example uses 45 dB
    esp_codec_dev_set_in_gain(g_record_handle, 45.0f);
    esp_codec_dev_set_out_vol(g_playback_handle, 100);

    // Re-enable I2S channels that es8311_prealloc_i2s() disabled to keep
    // clocks silent during WiFi RF calibration.  esp_codec_dev_open() above
    // may or may not call i2s_channel_enable() depending on the codec driver,
    // so do it explicitly to guarantee the channels are live.
    if (g_tx) {
        esp_err_t en_ret = i2s_channel_enable(g_tx);
        if (en_ret != ESP_OK && en_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "i2s_channel_enable(tx) failed: %s", esp_err_to_name(en_ret));
        }
    }
    if (g_rx) {
        esp_err_t en_ret = i2s_channel_enable(g_rx);
        if (en_ret != ESP_OK && en_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "i2s_channel_enable(rx) failed: %s", esp_err_to_name(en_ret));
        }
    }

    g_es8311_inited = true;
    ESP_LOGI(TAG, "ES8311 initialized (esp_codec_dev, %d Hz, stereo 16-bit)", AUDIO_SAMPLE_RATE);

    return ESP_OK;

fail:
    esp_codec_dev_delete(g_record_handle);
    g_record_handle = NULL;
    esp_codec_dev_delete(g_playback_handle);
    g_playback_handle = NULL;
    audio_codec_delete_codec_if(g_codec_if);
    g_codec_if = NULL;
    audio_codec_delete_data_if(g_data_if);
    g_data_if = NULL;
    audio_codec_delete_ctrl_if(g_ctrl_if);
    g_ctrl_if = NULL;
    es8311_i2s_deinit();
    return ESP_FAIL;
}

void es8311_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    if (g_playback_handle) {
        esp_codec_dev_set_out_vol(g_playback_handle, vol);
    }
}

void es8311_deinit(void)
{
    if (!g_es8311_inited) return;
    g_es8311_inited = false;
    if (g_record_handle) {
        esp_codec_dev_close(g_record_handle);
        esp_codec_dev_delete(g_record_handle);
        g_record_handle = NULL;
    }
    if (g_playback_handle) {
        esp_codec_dev_close(g_playback_handle);
        esp_codec_dev_delete(g_playback_handle);
        g_playback_handle = NULL;
    }
    if (g_codec_if) {
        audio_codec_delete_codec_if(g_codec_if);
        g_codec_if = NULL;
    }
    if (g_data_if) {
        audio_codec_delete_data_if(g_data_if);
        g_data_if = NULL;
    }
    if (g_ctrl_if) {
        audio_codec_delete_ctrl_if(g_ctrl_if);
        g_ctrl_if = NULL;
    }
    es8311_i2s_deinit();
    ESP_LOGI(TAG, "ES8311 deinitialized");
}

esp_codec_dev_handle_t es8311_get_playback_handle(void)
{
    return g_playback_handle;
}

esp_codec_dev_handle_t es8311_get_record_handle(void)
{
    return g_record_handle;
}

i2s_chan_handle_t es8311_get_rx_handle(void)
{
    return g_rx;
}
