#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"

static const char *TAG = "SYSTEM";

#define TCA9554_ADDR        0x38
#define TCA9554_INPUT       0x00
#define TCA9554_OUTPUT      0x01
#define TCA9554_CONFIG      0x03

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t tca9554_dev = NULL;

void *get_i2c_bus_handle(void)
{
    return (void *)i2c_bus;
}

static void tca9554_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t data[2] = {reg, val};
    i2c_master_transmit(tca9554_dev, data, 2, 100);
}

uint8_t tca9554_read_input(void)
{
    uint8_t reg = TCA9554_INPUT;
    uint8_t val;
    i2c_master_transmit(tca9554_dev, &reg, 1, 100);
    i2c_master_receive(tca9554_dev, &val, 1, 100);
    return val;
}

static uint8_t tca9554_read_output(void)
{
    uint8_t reg = TCA9554_OUTPUT;
    uint8_t val;
    i2c_master_transmit(tca9554_dev, &reg, 1, 100);
    i2c_master_receive(tca9554_dev, &val, 1, 100);
    return val;
}

static void tca9554_write_output(uint8_t val)
{
    tca9554_write_reg(TCA9554_OUTPUT, val);
}

void board_power_epd_on(void)
{
    uint8_t val = tca9554_read_output();
    val |= (1 << EXIO_EPD_PWR);
    tca9554_write_output(val);
    ESP_LOGI(TAG, "EPD power ON");
}

void board_power_audio_on(void)
{
    uint8_t val = tca9554_read_output();
    val |= (1 << EXIO_AUDIO_PWR) | (1 << EXIO_AMP_ENABLE);
    tca9554_write_output(val);
    ESP_LOGI(TAG, "Audio power ON");
}

void board_power_vbat_on(void)
{
    uint8_t val = tca9554_read_output();
    val |= (1 << EXIO_VBAT_PWR);
    tca9554_write_output(val);
    ESP_LOGI(TAG, "VBAT power ON");
}

void system_init(void)
{
    ESP_LOGI(TAG, "Initializing system...");

    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus));

    i2c_device_config_t tca_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9554_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &tca_cfg, &tca9554_dev));

    uint8_t config_val = 0xFF;
    config_val &= ~(1 << EXIO_EPD_PWR);
    config_val &= ~(1 << EXIO_AUDIO_PWR);
    config_val &= ~(1 << EXIO_AMP_ENABLE);
    config_val &= ~(1 << EXIO_LED);
    config_val &= ~(1 << EXIO_VBAT_PWR);
    tca9554_write_reg(TCA9554_CONFIG, config_val);

    tca9554_write_output(0);

    board_power_epd_on();
    board_power_audio_on();
    vTaskDelay(pdMS_TO_TICKS(100));
    board_power_vbat_on();

    ESP_LOGI(TAG, "System initialized");
}
