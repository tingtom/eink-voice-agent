#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "app_config.h"

static const char *TAG = "MIC";

static i2s_chan_handle_t mic_chan = NULL;
static bool is_running = false;

void mic_init(void)
{
    i2s_chan_config_t chan_cfg = {
        .id = MIC_I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = AUDIO_BUFFER_SIZE,
        .auto_clear = true,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &mic_chan));

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
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_I2S_BCLK_GPIO,
            .ws = MIC_I2S_LRCK_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_I2S_DATA_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(mic_chan, &std_cfg));
    ESP_LOGI(TAG, "Microphone initialized (I2S, %d Hz, mono)", AUDIO_SAMPLE_RATE);
}

void mic_start(void)
{
    if (!is_running) {
        ESP_ERROR_CHECK(i2s_channel_enable(mic_chan));
        is_running = true;
        ESP_LOGI(TAG, "Microphone started");
    }
}

void mic_stop(void)
{
    if (is_running) {
        ESP_ERROR_CHECK(i2s_channel_disable(mic_chan));
        is_running = false;
        ESP_LOGI(TAG, "Microphone stopped");
    }
}

esp_err_t mic_read(int16_t *buffer, size_t samples, size_t *read)
{
    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(mic_chan, buffer, samples * sizeof(int16_t), &bytes_read, portMAX_DELAY);
    if (ret == ESP_OK && read) {
        *read = bytes_read / sizeof(int16_t);
    }
    return ret;
}
