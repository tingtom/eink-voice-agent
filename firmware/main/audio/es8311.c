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

    // GPIO I2C noise immunity (from Espressif driver)
    {0x44, 0x08},
    {0x44, 0x08},

    // Power up LDO + analog blocks
    {0x31, 0x08},   // LDO D2A enable
    {0x30, 0x00},   // LDO control
    {0x29, 0x64},   // ADC/DAC bias current
    {0x2A, 0x7C},   // MIC_LDO_EN, MIC1L input enable, MICBIAS_EN

    // Power on all analog stages
    {0x2B, 0x14},   // Output stage power
    {0x2C, 0xFF},   // Additional power blocks
    {0x2D, 0x30},   // Bias current setup

    // Clock for 16kHz I2S slave (MCLK=4.096MHz, LRCK=256) — V2 registers per Espressif driver
    {0x01, 0x30},   // CLK_MANAGER_REG01: MCLK src, clk enable
    {0x02, 0x00},   // CLK_MANAGER_REG02: pre_div=1, pre_multi=1
    {0x03, 0x10},   // CLK_MANAGER_REG03: ADC fs_mode=0, OSR=16
    {0x04, 0x20},   // CLK_MANAGER_REG04: DAC OSR=32
    {0x05, 0x00},   // CLK_MANAGER_REG05: ADC/DAC div=1
    {0x06, 0x03},   // CLK_MANAGER_REG06: BCLK divider=4 (bclk_div-1)
    {0x07, 0x00},   // CLK_MANAGER_REG07: LRCK high div
    {0x08, 0xFF},   // CLK_MANAGER_REG08: LRCK low div=255

    // ADC input path — single-ended MIC1, +18dB PGA (V1 register)
    {0x04, 0x06},   // INPUT_SEL=00(MIC1), PGA_GAIN=11(+18dB)

    // I2S format — Philips (standard I2S), 16-bit (V2 registers)
    {0x09, 0x0C},   // SDPIN_REG09: DAC format Philips 16-bit
    {0x0A, 0x0C},   // SDPOUT_REG0A: ADC format Philips 16-bit

    // System registers (V2)
    {0x0B, 0x00},   // SYSTEM_REG0B
    {0x0C, 0x00},   // SYSTEM_REG0C
    {0x10, 0x1F},   // SYSTEM_REG10
    {0x11, 0x7F},   // SYSTEM_REG11

    // ADC power/enable (critical - from Espressif driver start())
    {0x0E, 0x02},   // SYSTEM_REG0E: ADC power up
    {0x12, 0x00},   // SYSTEM_REG12: DAC power
    {0x14, 0x1A},   // SYSTEM_REG14: PGA gain + analog enable
    {0x0D, 0x01},   // SYSTEM_REG0D: power control

    // ADC/DAC config
    {0x15, 0x40},   // ADC_REG15: ADC ramp rate
    {0x16, 0x24},   // ADC_REG16: MIC gain (0dB + timer)
    {0x17, 0xBF},   // ADC_REG17: ADC volume 0dB
    {0x37, 0x08},   // DAC_REG37: DAC ramp rate

    // ALC off
    {0x1A, 0x00},
    {0x1B, 0x0A},   // ADC_REG1B: ALC/HPF
    {0x1C, 0x6A},   // ADC_REG1C: EQ/HPF

    // DAC output path
    {0x14, 0x1A},   // DAC enable, de-emph off, soft-ramp off, unmute
    {0x15, 0x00},   // DAC digital volume = 0dB (note: overlaps ADC_REG15)

    // Misc
    {0x28, 0x00},   // Analog low power off
    {0x2E, 0x10},   // DAC tuning normal
    {0x21, 0xF0},   // GPIO all output
    {0x45, 0x00},   // GP_REG45

    // Final volume reset
    {0x05, 0x00},   // ADC volume = 0dB
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
