#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "app_config.h"

static const char *TAG = "SYSTEM";

#define TCA9554_ADDR        0x38
#define TCA9554_INPUT       0x00
#define TCA9554_OUTPUT      0x01
#define TCA9554_CONFIG      0x03

static void tca9554_write_reg(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
}

void board_power_epd_on(void)
{
    uint8_t val;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, TCA9554_OUTPUT, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &val, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    val |= (1 << EXIO_EPD_PWR);
    tca9554_write_reg(TCA9554_OUTPUT, val);
    ESP_LOGI(TAG, "EPD power ON");
}

void board_power_audio_on(void)
{
    uint8_t val;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, TCA9554_OUTPUT, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &val, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    val |= (1 << EXIO_AUDIO_PWR) | (1 << EXIO_AMP_ENABLE);
    tca9554_write_reg(TCA9554_OUTPUT, val);
    ESP_LOGI(TAG, "Audio power ON");
}

void board_power_vbat_on(void)
{
    uint8_t val;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, TCA9554_OUTPUT, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &val, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    val |= (1 << EXIO_VBAT_PWR);
    tca9554_write_reg(TCA9554_OUTPUT, val);
    ESP_LOGI(TAG, "VBAT power ON");
}

void system_init(void)
{
    ESP_LOGI(TAG, "Initializing system...");

    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

    uint8_t config_val = 0xFF;
    config_val &= ~(1 << EXIO_EPD_PWR);
    config_val &= ~(1 << EXIO_AUDIO_PWR);
    config_val &= ~(1 << EXIO_AMP_ENABLE);
    config_val &= ~(1 << EXIO_LED);
    config_val &= ~(1 << EXIO_VBAT_PWR);
    tca9554_write_reg(TCA9554_CONFIG, config_val);

    tca9554_write_reg(TCA9554_OUTPUT, 0);

    board_power_epd_on();
    board_power_audio_on();
    vTaskDelay(pdMS_TO_TICKS(100));    // wait for ES8311 power-up
    board_power_vbat_on();

    ESP_LOGI(TAG, "System initialized");
}
