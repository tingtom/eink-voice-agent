/**
 * Application Configuration
 *
 * Pin definitions, WiFi credentials, API endpoints, and tuning parameters.
 * Copy this to app_config_local.h and fill in your values.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// ── Device Identity ──────────────────────────────────────────
#define DEVICE_NAME         "Merlin"
#define DEVICE_ID           "eink-agent-001"
#define DEVICE_AUTH_TOKEN   "change-me-in-production"

// ── WiFi ─────────────────────────────────────────────────────
#define WIFI_SSID           "your-wifi-ssid"
#define WIFI_PASSWORD       "your-wifi-password"
#define WIFI_MAX_RETRIES    5

// ── Hermes Server ────────────────────────────────────────────
#define HERMES_HTTP_URL     "http://192.168.1.10:8123"
#define HERMES_WS_URL       "ws://192.168.1.10:8123/api/device/ws"

// ── Pin Definitions (ESP32-C6) ──────────────────────────────
// I2S Microphone (INMP441)
#define MIC_I2S_BCLK_GPIO   4
#define MIC_I2S_LRCK_GPIO   5
#define MIC_I2S_DATA_GPIO   6
#define MIC_I2S_PORT        I2S_NUM_0

// I2S Speaker (MAX98357A)
#define SPK_I2S_BCLK_GPIO   18
#define SPK_I2S_LRCK_GPIO   19
#define SPK_I2S_DATA_GPIO   20
#define SPK_I2S_PORT        I2S_NUM_1
#define SPK_ENABLE_GPIO     21

// e-Paper Display (SPI)
#define EPAPER_MOSI_GPIO    7
#define EPAPER_CLK_GPIO     8
#define EPAPER_CS_GPIO      9
#define EPAPER_DC_GPIO      10
#define EPAPER_RST_GPIO     11
#define EPAPER_BUSY_GPIO    12
#define EPAPER_SPI_HOST     SPI2_HOST

// Buttons
#define BUTTON_UP_GPIO      13
#define BUTTON_DOWN_GPIO    14
#define BUTTON_SELECT_GPIO  15
#define BUTTON_BACK_GPIO    16

// Battery
#define BATTERY_ADC_GPIO    0  // ADC channel for battery voltage
#define BATTERY_MIN_MV      3300
#define BATTERY_MAX_MV      4200

// Charging (TP4056)
#define CHARGE_STATUS_GPIO  1

// ── Audio ────────────────────────────────────────────────────
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_BUFFER_SIZE   4096
#define AUDIO_CHANNELS      1

// ── Wake Word ────────────────────────────────────────────────
#define WAKE_WORD           "hey merlin"
#define WAKE_WORD_SENSITIVITY  0.7f
#define WAKE_WORD_MODEL_PATH   "/models/wake_word_model.h"

// ── Power Management ─────────────────────────────────────────
#define SLEEP_TIMEOUT_MS    300000  // 5 minutes idle before sleep
#define BATTERY_CHECK_INTERVAL_MS 300000  // Check battery every 5 min

// ── Display ──────────────────────────────────────────────────
#define DISPLAY_WIDTH       200
#define DISPLAY_HEIGHT      200
#define DISPLAY_FULL_REFRESH_MS  2000
#define DISPLAY_PARTIAL_REFRESH_MS 300

// ── WebSocket ────────────────────────────────────────────────
#define WS_RECONNECT_INTERVAL_MS 5000
#define WS_PING_INTERVAL_MS     30000
#define WS_TIMEOUT_MS           10000

#endif // APP_CONFIG_H
