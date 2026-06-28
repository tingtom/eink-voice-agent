/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "app_config.h"
#include "system_init.h"

static const char *TAG = "SYSTEM";

/* I2C expander addresses we probe */
#define PCA9555_ADDR        0x20    /* PCA9555 16-bit I/O expander (if present) */
#define PCA9554A_ADDR       0x3F    /* PCA9554A 8-bit I/O expander */
#define TCA9554_ADDR_FB     0x38    /* TCA9554 8-bit I/O expander */

/* Register addresses (common to 8-bit expanders) */
#define TCA9554_INPUT        0x00
#define TCA9554_OUTPUT       0x01
#define TCA9554_CONFIG       0x03

/* PCA9555-specific registers (if we ever need to distinguish) */
#define PCA9555_OUTPUT       0x02
#define PCA9555_CONFIG       0x06

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t tca9554_dev = NULL;
static bool tca9554_present = false;
static bool is_pca9555 = false;   /* true if detected chip is PCA9555 */

/* ------------------------------------------------------------------ */
/* Helper: probe a single I2C address                                 */
static bool i2c_probe(i2c_master_bus_handle_t bus, uint8_t addr)
{
    return i2c_master_probe(bus, addr, 50) == ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Low-level I2C register access (write 1 byte, read 1 byte)          */
static esp_err_t tca9554_safe_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t data[2] = { reg, val };
    esp_err_t ret = i2c_master_transmit(tca9554_dev, data, 2, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 write reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t tca9554_safe_read(uint8_t reg, uint8_t *val)
{
    esp_err_t ret = i2c_master_transmit(tca9554_dev, &reg, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 reg 0x%02X transmit failed: %s", reg, esp_err_to_name(ret));
        return ret;
    }
    ret = i2c_master_receive(tca9554_dev, val, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 reg 0x%02X read failed: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

static void tca9554_write_reg(uint8_t reg, uint8_t val)
{
    tca9554_safe_write_reg(reg, val);
}

/* ------------------------------------------------------------------ */
/* Select the correct output/config register depending on chip type   */
static uint8_t get_output_reg(void)
{
    return is_pca9555 ? PCA9555_OUTPUT : TCA9554_OUTPUT;
}

static uint8_t get_config_reg(void)
{
    return is_pca9555 ? PCA9555_CONFIG : TCA9554_CONFIG;
}

/* ------------------------------------------------------------------ */
/* Public read functions (match the header)                           */
uint8_t tca9554_read_input(void)
{
    if (!tca9554_present) return 0xFF;
    uint8_t val;
    if (tca9554_safe_read(TCA9554_INPUT, &val) != ESP_OK) return 0xFF;
    return val;
}

uint8_t tca9554_read_output(void)
{
    if (!tca9554_present) return 0;
    uint8_t val;
    if (tca9554_safe_read(get_output_reg(), &val) != ESP_OK) return 0;
    return val;
}

/* ------------------------------------------------------------------ */
/* Write helpers                                                      */
static void tca9554_write_output(uint8_t val)
{
    tca9554_write_reg(get_output_reg(), val);
}

/* ------------------------------------------------------------------ */
/* I2C bus initialisation                                             */
void i2c_bus_init(void)
{
    if (i2c_bus) {
        ESP_LOGD(TAG, "I2C bus already initialized");
        return;
    }
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus));
    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d SCL=%d)", I2C_SDA_GPIO, I2C_SCL_GPIO);
}

void *get_i2c_bus_handle(void)
{
    return i2c_bus;
}

/* ------------------------------------------------------------------ */
/* I2C bus scan (for debugging)                                       */
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

/* ------------------------------------------------------------------ */
/* System initialisation                                              */
void system_init(void)
{
    ESP_LOGI(TAG, "Initializing system...");
    i2c_bus_init();

    /* Probe for I/O expander at known addresses */
    uint8_t expander_addr = 0;
    bool is_pca9555_detected = false;

    if (i2c_probe(i2c_bus, PCA9555_ADDR)) {
        expander_addr = PCA9555_ADDR;
        /* Tentatively assume it's NOT a PCA9555 (we treat as 8-bit expander)
         * unless we can verify otherwise later. For now, all known boards
         * use a TCA9554/PCA9554A-compatible device at 0x20. */
        is_pca9555_detected = false;
        ESP_LOGI(TAG, "Found I/O expander at 0x%02X", expander_addr);
    } else if (i2c_probe(i2c_bus, PCA9554A_ADDR)) {
        expander_addr = PCA9554A_ADDR;
        is_pca9555_detected = false;
        ESP_LOGI(TAG, "PCA9554A (I/O expander) found at 0x%02X", expander_addr);
    } else if (i2c_probe(i2c_bus, TCA9554_ADDR_FB)) {
        expander_addr = TCA9554_ADDR_FB;
        is_pca9555_detected = false;
        ESP_LOGI(TAG, "TCA9554 found at 0x%02X", expander_addr);
    }

    if (expander_addr == 0) {
        ESP_LOGW(TAG,
                 "I/O expander NOT FOUND at 0x%02X, 0x%02X or 0x%02X — "
                 "e-paper power, audio amp, and charger detection disabled",
                 PCA9555_ADDR, PCA9554A_ADDR, TCA9554_ADDR_FB);
        tca9554_present = false;
        return;
    }

    tca9554_present = true;
    is_pca9555 = is_pca9555_detected;

    /* Add the device to the I2C bus */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = expander_addr,
        .scl_speed_hz = 400000,
    };
    esp_err_t add_ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &tca9554_dev);
    if (add_ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to add I/O expander device at 0x%02X: %s",
                 expander_addr, esp_err_to_name(add_ret));
        tca9554_present = false;
        return;
    }
    ESP_LOGI(TAG, "I/O expander added at 0x%02X", expander_addr);

    /* --------------------------------------------------------------
     * Configure the I/O expander:
     *   - Make EPD_PWR(0), AUDIO_PWR(1), AMP_ENABLE(3), LED(4),
     *     VBAT_PWR(5) outputs (0 = output, 1 = input)
     *   - All other pins stay as inputs (default 1)
     *   - Start with all outputs low
     * -------------------------------------------------------------- */
    uint8_t config_val = 0xFF;
    config_val &= ~(1 << EXIO_EPD_PWR);
    config_val &= ~(1 << EXIO_AUDIO_PWR);
    config_val &= ~(1 << EXIO_AMP_ENABLE);
    config_val &= ~(1 << EXIO_LED);
    config_val &= ~(1 << EXIO_VBAT_PWR);
    tca9554_write_reg(get_config_reg(), config_val);
    ESP_LOGI(TAG,
             "I/O expander config set to 0x%02X (outputs: EPD_PWR, AUDIO_PWR, AMP_ENABLE, LED, VBAT_PWR)",
             config_val);

    tca9554_write_reg(get_output_reg(), 0);
    ESP_LOGI(TAG, "I/O expander outputs initialized to 0x00");

    /* Power up essential peripherals (keep LED off initially to save power) */
    ESP_LOGI(TAG, "Turning on EPD power");
    gpio_set_level(EXIO_EPD_PWR, 1);
    ESP_LOGI(TAG, "Turning on audio power");
    gpio_set_level(EXIO_AUDIO_PWR, 1);
    ESP_LOGI(TAG, "Turning on VBAT power");
    gpio_set_level(EXIO_VBAT_PWR, 1);
}

/* ------------------------------------------------------------------ */
/* Power‑control helpers                                              */
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

void board_power_epd_off(void)
{
    if (!tca9554_present) {
        ESP_LOGW(TAG, "TCA9554 absent, skipping EPD power");
        return;
    }
    uint8_t val = tca9554_read_output();
    val &= ~(1 << EXIO_EPD_PWR);
    tca9554_write_output(val);
    ESP_LOGI(TAG, "EPD power OFF");
}

void board_power_audio_on(void)
{
    if (!tca9554_present) {
        ESP_LOGW(TAG,
                 "No TCA9554 — audio power control unavailable, assuming always-on");
        return;
    }
    uint8_t val = tca9554_read_output();
    val |= (1 << EXIO_AUDIO_PWR) | (1 << EXIO_AMP_ENABLE);
    tca9554_write_output(val);
    ESP_LOGI(TAG, "Audio power ON");
}

void board_power_audio_off(void)
{
    if (!tca9554_present) {
        /* Fallback: try to control via GPIO if expander missing (should not happen) */
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

void board_power_vbat_off(void)
{
    if (!tca9554_present) {
        ESP_LOGW(TAG, "TCA9554 absent, skipping VBAT power");
        return;
    }
    uint8_t val = tca9554_read_output();
    val &= ~(1 << EXIO_VBAT_PWR);
    tca9554_write_output(val);
    ESP_LOGI(TAG, "VBAT power OFF");
}

void board_power_led_on(void)
{
    if (!tca9554_present) {
        ESP_LOGW(TAG, "TCA9554 absent, skipping LED");
        return;
    }
    uint8_t val = tca9554_read_output();
    val |= (1 << EXIO_LED);
    tca9554_write_output(val);
    ESP_LOGI(TAG, "LED ON");
}

void board_power_led_off(void)
{
    if (!tca9554_present) {
        ESP_LOGW(TAG, "TCA9554 absent, skipping LED");
        return;
    }
    uint8_t val = tca9554_read_output();
    val &= ~(1 << EXIO_LED);
    tca9554_write_output(val);
    ESP_LOGI(TAG, "LED OFF");
}

/* ------------------------------------------------------------------ */
/* System status helpers                                              */
bool system_tca9554_present(void)
{
    return tca9554_present;
}

/* ------------------------------------------------------------------ */
void system_deinit(void)
{
    if (tca9554_dev) {
        i2c_master_bus_rm_device(tca9554_dev);
        tca9554_dev = NULL;
    }
    if (i2c_bus) {
        i2c_del_master_bus(i2c_bus);
        i2c_bus = NULL;
    }
}