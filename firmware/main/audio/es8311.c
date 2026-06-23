#include <string.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "app_config.h"

static const char *TAG = "ES8311";

#define ES8311_I2C_ADDR     0x18

static uint8_t current_volume = 80;

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES8311_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t read_reg(uint8_t reg, uint8_t *val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES8311_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES8311_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, val, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

typedef struct { uint8_t reg; uint8_t val; } reg_cfg;

static const reg_cfg init_seq[] = {
    // Reset to defaults
    {0x00, 0x3F},
    {0x00, 0x00},

    // Power up LDO + analog blocks
    {0x31, 0x08},   // LDO D2A enable
    {0x30, 0x00},   // LDO control
    {0x29, 0x64},   // ADC/DAC bias current
    {0x2A, 0x70},   // MIC_LDO enable, MIC bias on

    // Power on all analog stages
    {0x2B, 0x14},   // Output stage power
    {0x2C, 0xFF},   // Additional power blocks
    {0x2D, 0x30},   // Bias current setup

    // Clock for 16kHz I2S slave (MCLK/LRCK = 256)
    {0x01, 0x00},   // MCLK_SRC=0 (use MCLK pin)
    {0x02, 0x10},   // FS_SEL=001 (16kHz at MCLK/LRCK=256)
    {0x03, 0x04},   // CKM auto clock detection

    // ADC input path — single-ended MIC1, 16-bit left-justified
    {0x04, 0x3A},   // PGA enable, +18dB, MIC1 single-ended with MICBIAS
    {0x05, 0x00},   // ADC digital volume = 0dB
    {0x06, 0x70},   // Left-justified, 16-bit

    // ALC off
    {0x08, 0x00},
    {0x09, 0x00},
    {0x0A, 0x00},
    {0x0B, 0x00},

    // DAC output path
    {0x14, 0x1A},   // DAC enable, de-emph off, soft-ramp off, unmute
    {0x15, 0x00},   // DAC digital volume = 0dB
    {0x16, 0x70},   // Left-justified, 16-bit

    // Misc
    {0x28, 0x00},   // Analog low power off
    {0x2E, 0x10},   // DAC tuning normal
    {0x21, 0xF0},   // GPIO all output

    // Final volume reset
    {0x05, 0x10},   // ADC volume = 0dB
    {0x15, 0x00},   // DAC volume = 0dB
};

esp_err_t es8311_init(void)
{
    uint8_t id = 0;
    esp_err_t ret = read_reg(0x40, &id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No ACK from ES8311 (I2C addr 0x%02X)", ES8311_I2C_ADDR);
        return ret;
    }
    ESP_LOGI(TAG, "ES8311 chip ID = 0x%02X", id);

    for (size_t i = 0; i < sizeof(init_seq) / sizeof(init_seq[0]); i++) {
        ret = write_reg(init_seq[i].reg, init_seq[i].val);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "reg 0x%02X write failed", init_seq[i].reg);
            return ret;
        }
    }

    ESP_LOGI(TAG, "ES8311 initialized (16 kHz, 16-bit, I2S slave)");
    return ESP_OK;
}

void es8311_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    current_volume = vol;

    // DAC volume register: 0x15 (0dB = 0x00, +6dB = 0x30, -inf = 0xFF)
    // Scale 0-100 to 0x00 (loudest) — 0xFF (mute) is too extreme
    // 0dB = 0x00, -1dB/step down
    uint8_t dac_vol = (100 - vol) * 2;  // 0→200, 100→0, clamp at ~0xBE (-95dB)
    if (dac_vol > 0xBE) dac_vol = 0xBE;
    write_reg(0x15, dac_vol);

    // ADC volume: 0x05 (0dB = 0x00, gain steps up to +6dB at 0x30)
    // Keep at 0dB for now
    write_reg(0x05, 0x00);
}
