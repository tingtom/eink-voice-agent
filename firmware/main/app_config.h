#ifndef APP_CONFIG_H
#define APP_CONFIG_H

// ── Device Identity ──────────────────────────────────────────
#define DEVICE_NAME         "Jeff"
#define DEVICE_ID           "eink-agent-001"
#define DEVICE_AUTH_TOKEN   "change-me-in-production"

// ── WiFi (credentials provisioned to NVS at runtime) ─────────

// ── Hermes Server (overridden by mDNS discovery if available) ─
#define HERMES_HTTP_URL     "http://192.168.1.25:8123"
#define HERMES_WS_URL       "ws://192.168.1.25:8123/api/device/ws"

// ── Pin Definitions (Waveshare ESP32-C6-ePaper-1.54) ────────
// I2C Bus (shared by TCA9554, PCF85063 RTC, SHTC3 sensor)
#define I2C_SDA_GPIO        18
#define I2C_SCL_GPIO        8
#define I2C_PORT            I2C_NUM_0

// TCA9554 I/O Expander (I2C addr 0x38) - virtual GPIOs
#define EXIO_EPD_PWR        0
#define EXIO_AUDIO_PWR      1
#define EXIO_AMP_ENABLE     3
#define EXIO_LED            4
#define EXIO_VBAT_PWR       5

// e-Paper Display (SPI, SPI2_HOST)
#define EPAPER_MOSI_GPIO    5
#define EPAPER_MISO_GPIO    4
#define EPAPER_CLK_GPIO     6
#define EPAPER_CS_GPIO      7
#define EPAPER_DC_GPIO      15
#define EPAPER_RST_GPIO     11
#define EPAPER_BUSY_GPIO    10
#define EPAPER_SPI_HOST     SPI2_HOST

// I2S Audio (ES8311 codec)
#define I2S_MCLK_GPIO       19
#define I2S_BCLK_GPIO       21
#define I2S_WS_GPIO         22
#define I2S_DIN_GPIO        20
#define I2S_DOUT_GPIO       23
#define I2S_PORT            I2S_NUM_0

// Buttons (only 2 physical buttons on this board)
#define BUTTON_BOOT_GPIO    9
#define BUTTON_PWR_GPIO     2

// SD Card (shared SPI bus with e-paper)
#define SD_CS_GPIO          3
#define SD_MOSI_GPIO        5
#define SD_MISO_GPIO        4
#define SD_CLK_GPIO         6
#define SD_SPI_HOST         SPI2_HOST

// ── Audio ────────────────────────────────────────────────────
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_BUFFER_SIZE   4096
#define AUDIO_CHANNELS      1

// ── Voice Activity Detection ─────────────────────────────────
#define AUDIO_VAD_THRESHOLD         500
#define AUDIO_VAD_HANGOVER_MS       400
#define AUDIO_MAX_WAKE_WORD_CHECKS  40

// ── Wake Word ────────────────────────────────────────────────
#define WAKE_WORD           "hi jeff"
#define WAKE_WORD_SENSITIVITY  0.7f
#define WAKE_WORD_MODEL_PATH   "/models/tflite_learn_1037720_5.h"

// ── Battery ──────────────────────────────────────────────────
#define BATTERY_MIN_MV      3300
#define BATTERY_MAX_MV      4200

// ── Power Management ─────────────────────────────────────────
#define SLEEP_TIMEOUT_MS    300000
#define BATTERY_CHECK_INTERVAL_MS 300000

// ── Display ──────────────────────────────────────────────────
#define DISPLAY_WIDTH       200
#define DISPLAY_HEIGHT      200
#define DISPLAY_FULL_REFRESH_MS  2000
#define DISPLAY_PARTIAL_REFRESH_MS 300

// ── WebSocket ────────────────────────────────────────────────
#define WS_RECONNECT_INTERVAL_MS 5000
#define WS_PING_INTERVAL_SEC    30
#define WS_TIMEOUT_MS           10000

#endif
