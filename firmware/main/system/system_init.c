#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"

static const char *TAG = "SYSTEM";

#define PCA9555_ADDR        0x20    /* PCA9555 16-bit I/O expander on Waveshare board */
#define PCA9554A_ADDR       0x3F    /* fallback: PCA9554A 8-bit variant */
#define TCA9554_ADDR_FB     0x38    /* fallback: TCA9554 */
#define TCA9554_INPUT        0x00
#define TCA9554_OUTPUT       0x01
#define TCA9554_CONFIG       0x03
#define TCA9554_PROBE_TIMEOUT 50
#define TCA9554_XFER_TIMEOUT  100

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t tca9554_dev = NULL;
static bool tca9554_present = false;

static bool i2c_probe(i2c_master_bus_handle_t bus, uint8_t addr)
{
    return i2c_master_probe(bus, addr, TCA9554_PROBE_TIMEOUT) == ESP_OK;
}

static esp_err_t tca9554_safe_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t data[2] = { reg, val };
    esp_err_t ret = i2c_master_transmit(tca9554_dev, data, 2, TCA9554_XFER_TIMEOUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 write reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t tca9554_safe_read(uint8_t reg, uint8_t *val)
{
    esp_err_t ret = i2c_master_transmit(tca9554_dev, &reg, 1, TCA9554_XFER_TIMEOUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 reg 0x%02X transmit failed: %s", reg, esp_err_to_name(ret));
        return ret;
    }
    ret = i2c_master_receive(tca9554_dev, val, 1, TCA9554_XFER_TIMEOUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 reg 0x%02X read failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

static void tca9554_write_reg(uint8_t reg, uint8_t val)
{
    tca9554_safe_write_reg(reg, val);
}

uint8_t tca9554_read_input(void)
{
    if (!tca9554_present) return 0xFF;
    uint8_t val;
    if (tca9554_safe_read(TCA9554_INPUT, &val) != ESP_OK) return 0xFF;
    return val;
}

static uint8_t tca9554_read_output(void)
{
    if (!tca9554_present) return 0;
    uint8_t val;
    if (tca9554_safe_read(TCA9554_OUTPUT, &val) != ESP_OK) return 0;
    return val;
}

static void tca9554_write_output(uint8_t val)
{
    tca9554_write_reg(TCA9554_OUTPUT, val);
}

void board_power_epd_on(void)
{
    if (!tca9554_present) {
        ESP_LOGW(TAG, "TCA9554 absent, skipping EPD power");
        return;
    }
    uint8_t val = tca9554_read_output();
    val |= (1 << EXIO_EPD_PWR);
    tca9554_write_output(val);
    ESP_LOGI(TAG, "EPD power ON");
}

void board_power_audio_on(void)
{
    if (!tca9554_present) {
        ESP_LOGW(TAG, "No TCA9554 — audio power control unavailable, assuming always-on");
        return;
    }
    // Read-modify-write to avoid clobbering other bits (EPD_PWR, VBAT_PWR, LED).
    uint8_t val = tca9554_read_output();
    val |= (1 << EXIO_AUDIO_PWR) | (1 << EXIO_AMP_ENABLE);
    tca9554_write_output(val);
    ESP_LOGI(TAG, "Audio power ON");
}

void board_power_audio_off(void)
{
    if (!tca9554_present) {
        gpio_set_level(EXIO_AUDIO_PWR, 0);
        gpio_set_level(EXIO_AMP_ENABLE, 0);
        return;
    }
    uint8_t val = tca9554_read_output();
    val &= ~((1 << EXIO_AUDIO_PWR) | (1 << EXIO_AMP_ENABLE));
    tca9554_write_output(val);
    ESP_LOGI(TAG, "Audio power OFF");
}

void board_power_vbat_on(void)
{
    if (!tca9554_present) {
        ESP_LOGW(TAG, "TCA9554 absent, skipping VBAT power");
        return;
    }
    uint8_t val = tca9554_read_output();
    val |= (1 << EXIO_VBAT_PWR);
    tca9554_write_output(val);
    ESP_LOGI(TAG, "VBAT power ON");
}

bool system_tca9554_present(void)
{
    return tca9554_present;
}

void *get_i2c_bus_handle(void)
{
    return (void *)i2c_bus;
}

void i2c_bus_init(void)
{
    if (i2c_bus) {
        ESP_LOGD(TAG, "I2C bus already initialized");
        return;
    }
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d SCL=%d)", I2C_SDA_GPIO, I2C_SCL_GPIO);
}

void i2c_bus_scan(void)
{
    if (!i2c_bus) {
        ESP_LOGW(TAG, "I2C bus not initialized, cannot scan");
        return;
    }
    ESP_LOGI(TAG, "=== I2C bus scan ===");
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        if (i2c_master_probe(i2c_bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  I2C device found at 0x%02X", addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGW(TAG, "  No I2C devices found!");
    } else {
        ESP_LOGI(TAG, "  %d device(s) found", found);
    }
}

void system_init(void)
{
    ESP_LOGI(TAG, "Initializing system...");
    i2c_bus_init();

    ESP_LOGI(TAG, "Scanning I2C bus for I/O expander...");
    // Scan entire bus and log all responding addresses for debug
    for (uint8_t addr = 1; addr < 127; addr++) {
        if (i2c_master_probe(i2c_bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  I2C device found at 0x%02X", addr);
        }
    }
    if (i2c_probe(i2c_bus, PCA9555_ADDR)) {
        tca9554_present = true;
        ESP_LOGI(TAG, "PCA9555 (I/O expander) found at 0x%02X", PCA9555_ADDR);
    } else if (i2c_probe(i2c_bus, PCA9554A_ADDR)) {
        tca9554_present = true;
        ESP_LOGI(TAG, "PCA9554A (I/O expander) found at 0x%02X", PCA9554A_ADDR);
    } else if (i2c_probe(i2c_bus, TCA9554_ADDR_FB)) {
        tca9554_present = true;
        ESP_LOGI(TAG, "TCA9554 found at 0x%02X", TCA9554_ADDR_FB);
    } else {
        ESP_LOGW(TAG, "I/O expander NOT FOUND — audio amp, e-paper power, charger detection disabled");
    }

    if (tca9554_present) {
        uint8_t tca_addr = 0;
        if (i2c_probe(i2c_bus, PCA9555_ADDR))       tca_addr = PCA9555_ADDR;
        else if (i2c_probe(i2c_bus, PCA9554A_ADDR)) tca_addr = PCA9554A_ADDR;
        else if (i2c_probe(i2c_bus, TCA9554_ADDR_FB)) tca_addr = TCA9554_ADDR_FB;

        i2c_device_config_t tca_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = tca_addr,
            .scl_speed_hz = 400000,
        };
        esp_err_t add_ret = i2c_master_bus_add_device(i2c_bus, &tca_cfg, &tca9554_dev);
        if (add_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add I/O expander device at 0x%02X: %s", tca_addr, esp_err_to_name(add_ret));
            tca9554_present = false;
        } else {
            ESP_LOGI(TAG, "I/O expander added at 0x%02X", tca_addr);
        }
    }

    if (tca9554_present) {
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

        vTaskDelay(pdMS_TO_TICKS(200));
    }

    ESP_LOGI(TAG, "System initialized (I/O expander %s)", tca9554_present ? "present" : "absent");
}
