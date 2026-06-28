#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "system_init.h"

static const char *TAG = "ES8311";

static i2c_master_dev_handle_t es8311_i2c_dev = NULL;

static esp_err_t es8311_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(es8311_i2c_dev, buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t es8311_read_reg(uint8_t reg, uint8_t *val)
{
    esp_err_t ret = i2c_master_transmit(es8311_i2c_dev, &reg, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) return ret;
    return i2c_master_receive(es8311_i2c_dev, val, 1, pdMS_TO_TICKS(100));
}

static void es8311_set_bits(uint8_t reg, uint8_t mask, uint8_t val)
{
    uint8_t old = 0;
    esp_err_t ret = es8311_read_reg(reg, &old);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read reg 0x%02X: %s", reg, esp_err_to_name(ret));
        return;
    }
    uint8_t new_val = (old & ~mask) | (val & mask);
    es8311_write_reg(reg, new_val);
}

static esp_err_t es8311_i2c_init(void *bus_handle)
{
    i2c_master_dev_handle_t dev;
    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)bus_handle;
    esp_err_t ret = i2c_master_bus_add_device(bus, &(i2c_device_config_t){
        .device_address = 0x18,
        .scl_speed_hz = 100000,
    }, &dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ES8311 to I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }
    es8311_i2c_dev = dev;
    ESP_LOGI(TAG, "ES8311 I2C device added at 0x18");
    return ESP_OK;
}

static esp_err_t es8311_chip_id_check(void)
{
    uint8_t id = 0;
    esp_err_t ret = es8311_read_reg(0xFD, &id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID: %s", esp_err_to_name(ret));
        return ret;
    }
    if (id != 0x83) {
        ESP_LOGE(TAG, "Bad ES8311 chip ID: 0x%02X (expected 0x83)", id);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "ES8311 chip ID verified: 0x%02X", id);
    return ESP_OK;
}

static void es8311_reset(void)
{
    es8311_write_reg(0x00, 0x1F);
    vTaskDelay(pdMS_TO_TICKS(100));
    es8311_write_reg(0x00, 0x00);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static esp_err_t es8311_clock_config(void)
{
    // MCLK division: 256fs for 16kHz → MCLK = 4.096MHz (assuming 256*16k)
    // Set MCLK=256*fs, no MCLK input (internal)
    esp_err_t ret;
    ret = es8311_write_reg(0x01, 0x3F);  // CLK_MANAGER: MCLK from internal, divide by 256
    if (ret != ESP_OK) return ret;
    ret = es8311_write_reg(0x02, 0x00);  // slave mode
    if (ret != ESP_OK) return ret;
    ret = es8311_write_reg(0x03, 0x10);  // 256fs
    if (ret != ESP_OK) return ret;
    ret = es8311_write_reg(0x04, 0x10);  // sample rate = 16k
    if (ret != ESP_OK) return ret;
    ret = es8311_write_reg(0x05, 0x00);  // bclk_div = 1
    if (ret != ESP_OK) return ret;

    // WaveShare reference for 16kHz:
    // REG01=0x3F, REG02=0x00, REG03=0x10, REG04=0x10, REG05=0x00
    // REG06=0x00, REG07=0x7E
    ret = es8311_write_reg(0x06, 0x00);
    if (ret != ESP_OK) return ret;
    ret = es8311_write_reg(0x07, 0x7E);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

static esp_err_t es8311_system_config(void)
{
    esp_err_t ret;
    // System reg 0x0B: all off
    ret = es8311_write_reg(0x0B, 0x00);
    if (ret != ESP_OK) return ret;

    // System reg 0x0C: DACR→LOUT1, DACL→ROUT1
    ret = es8311_write_reg(0x0C, 0x18);
    if (ret != ESP_OK) return ret;

    // System reg 0x10: 0x1F
    ret = es8311_write_reg(0x10, 0x1F);
    if (ret != ESP_OK) return ret;

    // System reg 0x11: 0x7F
    ret = es8311_write_reg(0x11, 0x7F);
    if (ret != ESP_OK) return ret;

    // Reset: power down, then release
    ret = es8311_write_reg(0x00, 0x80);
    if (ret != ESP_OK) return ret;

    uint8_t val = 0;
    ret = es8311_read_reg(0x00, &val);
    if (ret != ESP_OK) return ret;
    // Slave mode: bit 6 = 0
    val &= ~0x40;
    ret = es8311_write_reg(0x00, val);
    if (ret != ESP_OK) return ret;

    // Clock manager: use MCLK, not inverted
    ret = es8311_write_reg(0x01, 0x3F);
    if (ret != ESP_OK) return ret;

    // SCLK not inverted
    ret = es8311_read_reg(0x06, &val);
    if (ret != ESP_OK) return ret;
    val &= ~0x20;
    ret = es8311_write_reg(0x06, val);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

static esp_err_t es8311_adc_config(void)
{
    esp_err_t ret;
    ret = es8311_write_reg(0x13, 0x10);
    if (ret != ESP_OK) return ret;
    ret = es8311_write_reg(0x1B, 0x0A);
    if (ret != ESP_OK) return ret;
    ret = es8311_write_reg(0x1C, 0x6A);
    if (ret != ESP_OK) return ret;
    ret = es8311_write_reg(0x16, 0x24);
    if (ret != ESP_OK) return ret;

    // GPIO: enable ADCL+DACR reference
    ret = es8311_write_reg(0x44, 0x58);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

static esp_err_t es8311_dac_config(void)
{
    esp_err_t ret;
    // DAC output routing: DACR→LOUT1, DACL→ROUT1
    ret = es8311_write_reg(0x0C, 0x18);
    if (ret != ESP_OK) return ret;

    // DAC enable
    ret = es8311_write_reg(0x12, 0x01);
    if (ret != ESP_OK) return ret;

    // Unmute L/R
    ret = es8311_write_reg(0x31, 0x00);
    if (ret != ESP_OK) return ret;

    // Volume: 0x3F = 0dB (max)
    ret = es8311_write_reg(0x32, 0x3F);
    if (ret != ESP_OK) return ret;

    // Analog: DAC+ADC on
    ret = es8311_write_reg(0x0D, 0xFA);
    if (ret != ESP_OK) return ret;

    // Analog block on
    ret = es8311_write_reg(0x0E, 0x06);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

static esp_err_t es8311_i2s_config(void)
{
    // SDPIN (REG09): I2S format = standard I2S
    es8311_set_bits(0x09, 0xE0, 0x00);
    // SDPOUT (REG0A): I2S format = standard I2S
    es8311_set_bits(0x0A, 0xE0, 0x00);

    return ESP_OK;
}

esp_err_t es8311_init(i2s_chan_handle_t tx_handle)
{
    (void)tx_handle;
    esp_err_t ret;

    void *bus_handle = get_i2c_bus_handle();
    if (!bus_handle) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ret = es8311_i2c_init(bus_handle);
    if (ret != ESP_OK) return ret;

    ret = es8311_chip_id_check();
    if (ret != ESP_OK) return ret;

    es8311_reset();
    ESP_LOGI(TAG, "Work in Slave mode");

    ret = es8311_clock_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Clock config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = es8311_system_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = es8311_adc_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = es8311_dac_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DAC config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = es8311_i2s_config();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "ES8311 initialized (raw I2C)");
    return ESP_OK;
}

void es8311_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    uint8_t reg = (uint8_t)((vol / 100.0f) * 0x3F);
    es8311_write_reg(0x32, reg);
}

void es8311_deinit(void)
{
    if (es8311_i2c_dev) {
        i2c_master_bus_rm_device(es8311_i2c_dev);
        es8311_i2c_dev = NULL;
    }
}

esp_codec_dev_handle_t es8311_get_handle(void)
{
    return NULL;
}
