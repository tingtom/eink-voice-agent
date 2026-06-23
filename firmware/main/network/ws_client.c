#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "lwip/inet.h"
#include "app_config.h"
#include "base64.h"
#include "ws_client.h"

static const char *TAG = "WS";

static esp_websocket_client_handle_t client = NULL;
static ws_message_callback_t message_cb = NULL;
static char auth_token[128] = {0};

static char discovered_url[256] = {0};

static esp_err_t discover_gateway(void)
{
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr("_eink-voice-gateway", "_tcp", 3000, 1, &results);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS gateway query failed: %s", esp_err_to_name(err));
        return err;
    }
    if (!results) {
        ESP_LOGW(TAG, "No Hermes gateway found via mDNS");
        return ESP_FAIL;
    }

    mdns_result_t *r = results;
    if (r->addr && r->addr->addr.type == IPADDR_TYPE_V4) {
        char ip_str[16];
        esp_ip4_addr_t ip = r->addr->addr.u_addr.ip4;
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip));
        snprintf(discovered_url, sizeof(discovered_url),
                 "ws://%s:%d/api/device/ws", ip_str, r->port);
        ESP_LOGI(TAG, "Discovered gateway at %s", discovered_url);
    } else {
        ESP_LOGW(TAG, "Gateway found but no IPv4 address");
        mdns_query_results_free(results);
        return ESP_FAIL;
    }
    mdns_query_results_free(results);
    return ESP_OK;
}

static void ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *data)
{
    (void)handler_args;
    (void)base;

    esp_websocket_event_data_t *event = (esp_websocket_event_data_t *)data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
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

    // Try mDNS discovery first; fall back to compiled-in URL
    const char *connect_url = url;
    if (discover_gateway() == ESP_OK) {
        connect_url = discovered_url;
    }

    esp_websocket_client_config_t cfg = {
        .uri = connect_url,
        .keep_alive_interval = WS_PING_INTERVAL_SEC,
        .network_timeout_ms = WS_TIMEOUT_MS,
        .disable_auto_reconnect = false,
        .reconnect_timeout_ms = WS_RECONNECT_INTERVAL_MS,
    };

    client = esp_websocket_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create WebSocket client");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL));
    ESP_ERROR_CHECK(esp_websocket_client_start(client));

    ESP_LOGI(TAG, "WebSocket client initialized, connecting to %s", connect_url);
    return ESP_OK;
}

esp_err_t ws_client_send_audio(const uint8_t *data, size_t len)
{
    return ws_client_send_audio_mode(data, len, NULL);
}

esp_err_t ws_client_send_json(const char *json_str)
{
    if (!client || !esp_websocket_client_is_connected(client)) return ESP_FAIL;
    return esp_websocket_client_send_text(client, json_str, strlen(json_str), portMAX_DELAY);
}

esp_err_t ws_client_send_audio_mode(const uint8_t *data, size_t len, const char *mode)
{
    if (!client || !esp_websocket_client_is_connected(client)) return ESP_FAIL;

    size_t b64_len = ((len + 2) / 3) * 4 + 256;
    char *payload = malloc(b64_len);
    if (!payload) return ESP_ERR_NO_MEM;

    size_t encoded = base64_encode(data, len, payload + 128, b64_len - 128);
    size_t hdr_len = snprintf(payload, 128,
                              "{\"type\":\"audio\",\"mode\":\"%s\",\"session_id\":\"\",\"data\":\"",
                              mode ? mode : "");
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
    if (!client || !esp_websocket_client_is_connected(client)) return ESP_FAIL;
    return esp_websocket_client_send_text(client, text, strlen(text), portMAX_DELAY);
}

void ws_client_set_callback(ws_message_callback_t cb)
{
    message_cb = cb;
}

bool ws_client_is_connected(void)
{
    return client && esp_websocket_client_is_connected(client);
}
