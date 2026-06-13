#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "base64.h"

static const char *TAG = "WS";

static esp_websocket_client_handle_t client = NULL;
static ws_message_callback_t message_cb = NULL;
static bool connected = false;
static char auth_token[128] = {0};

static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *data)
{
    (void)handler_args;
    (void)base;

    esp_websocket_event_data_t *event = (esp_websocket_event_data_t *)data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            connected = true;
            ESP_LOGI(TAG, "WebSocket connected");
            {
                char auth[256];
                snprintf(auth, sizeof(auth),
                         "{\"type\":\"auth\",\"device_id\":\"%s\",\"token\":\"%s\"}",
                         DEVICE_ID, auth_token);
                esp_websocket_client_send_text(client, auth, strlen(auth), portMAX_DELAY);
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            connected = false;
            ESP_LOGW(TAG, "WebSocket disconnected");
            break;

        case WEBSOCKET_EVENT_DATA:
            if (message_cb && event->data_len > 0) {
                char *buf = malloc(event->data_len + 1);
                if (buf) {
                    memcpy(buf, event->data_ptr, event->data_len);
                    buf[event->data_len] = '\0';
                    message_cb(buf, event->data_len);
                    free(buf);
                }
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            break;

        default:
            break;
    }
}

esp_err_t ws_client_init(const char *url, const char *token)
{
    strncpy(auth_token, token, sizeof(auth_token) - 1);

    esp_websocket_client_config_t cfg = {
        .uri = url,
        .keep_alive_interval_ms = WS_PING_INTERVAL_MS,
        .network_timeout_ms = WS_TIMEOUT_MS,
        .disable_auto_reconnect = false,
        .reconnect_interval_ms = WS_RECONNECT_INTERVAL_MS,
    };

    client = esp_websocket_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create WebSocket client");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL));
    ESP_ERROR_CHECK(esp_websocket_client_start(client));

    ESP_LOGI(TAG, "WebSocket client initialized, connecting to %s", url);
    return ESP_OK;
}

esp_err_t ws_client_send_audio(const uint8_t *data, size_t len)
{
    if (!connected) return ESP_FAIL;

    size_t b64_len = ((len + 2) / 3) * 4 + 256;
    char *payload = malloc(b64_len);
    if (!payload) return ESP_ERR_NO_MEM;

    size_t encoded = base64_encode(data, len, payload + 128, b64_len - 128);
    size_t hdr_len = snprintf(payload, 128,
                              "{\"type\":\"audio\",\"session_id\":\"\",\"data\":\"");
    payload[hdr_len + encoded] = '"';
    payload[hdr_len + encoded + 1] = '}';
    payload[hdr_len + encoded + 2] = '\0';

    size_t total = hdr_len + encoded + 2;
    esp_err_t ret = esp_websocket_client_send_text(client, payload, total, portMAX_DELAY);
    free(payload);
    return ret;
}

esp_err_t ws_client_send_text(const char *text)
{
    if (!connected) return ESP_FAIL;
    return esp_websocket_client_send_text(client, text, strlen(text), portMAX_DELAY);
}

void ws_client_set_callback(ws_message_callback_t cb)
{
    message_cb = cb;
}

bool ws_client_is_connected(void)
{
    return connected;
}
