#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
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
static bool needs_reset = false;
static TimerHandle_t auth_timer = NULL;

static void auth_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (!client || !esp_websocket_client_is_connected(client)) return;
    char auth[256];
    snprintf(auth, sizeof(auth),
             "{\"type\":\"auth\",\"device_id\":\"%s\",\"token\":\"%s\"}",
             DEVICE_ID, auth_token);
    esp_err_t ret = esp_websocket_client_send_text(client, auth, strlen(auth), portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "deferred auth send failed: %s", esp_err_to_name(ret));
        needs_reset = true;
    }
}

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
            needs_reset = false;
            if (auth_timer) {
                xTimerStart(auth_timer, 0);
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected — library auto-reconnect will retry in %d ms", WS_RECONNECT_INTERVAL_MS);
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
        .ping_interval_sec = 15,
        .pingpong_timeout_sec = 60,
        .disable_pingpong_discon = true,
        .keep_alive_enable = true,
        .keep_alive_idle = 30,
        .keep_alive_interval = 10,
        .keep_alive_count = 3,
        .network_timeout_ms = WS_TIMEOUT_MS,
        .disable_auto_reconnect = false,
        .reconnect_timeout_ms = WS_RECONNECT_INTERVAL_MS,
    };

    client = esp_websocket_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create WebSocket client");
        return ESP_FAIL;
    }

    if (!auth_timer) {
        auth_timer = xTimerCreate("auth_timer", pdMS_TO_TICKS(500), pdFALSE, NULL, auth_timer_callback);
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

esp_err_t ws_client_send_text(const char *text)
{
    if (!client || !esp_websocket_client_is_connected(client)) {
        needs_reset = true;
        return ESP_FAIL;
    }
    esp_err_t ret = esp_websocket_client_send_text(client, text, strlen(text), portMAX_DELAY);
    if (ret != ESP_OK) needs_reset = true;
    return ret;
}

esp_err_t ws_client_send_audio_mode(const uint8_t *data, size_t len, const char *mode)
{
    if (!client || !esp_websocket_client_is_connected(client)) {
        needs_reset = true;
        return ESP_FAIL;
    }

    static char payload[2300]; // fits full 1600-byte audio packet via base64+header
    const size_t payload_cap = sizeof(payload);

    int hdr_len = snprintf(payload, payload_cap > 128 ? 128 : payload_cap,
                           "{\"type\":\"audio\",\"mode\":\"%s\",\"session_id\":\"\",\"data\":\"",
                           mode ? mode : "");
    if (hdr_len < 0 || (size_t)hdr_len >= payload_cap - 1) return ESP_ERR_INVALID_ARG;

    size_t remaining = payload_cap - (size_t)hdr_len - 2;
    size_t encoded = base64_encode(data, len, payload + hdr_len, remaining);
    if (encoded == 0 || encoded >= remaining) return ESP_ERR_INVALID_ARG;

    size_t total = hdr_len + encoded + 2;
    payload[total - 2] = '"';
    payload[total - 1] = '}';

    esp_err_t ret = esp_websocket_client_send_text(client, payload, total, portMAX_DELAY);
    if (ret != ESP_OK) needs_reset = true;
    return ret;
}

void ws_client_set_callback(ws_message_callback_t cb)
{
    message_cb = cb;
}

bool ws_client_is_connected(void)
{
    return client && esp_websocket_client_is_connected(client);
}

bool ws_client_needs_reset(void)
{
    return needs_reset;
}

void ws_client_destroy(void)
{
    if (auth_timer) {
        xTimerStop(auth_timer, 0);
    }
    if (client) {
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
        client = NULL;
        ESP_LOGI(TAG, "WebSocket client destroyed");
    }
}

void ws_client_reconnect(void)
{
    if (!client) return;
    if (esp_websocket_client_is_connected(client)) return;

    ESP_LOGW(TAG, "WebSocket disconnected, waiting for auto-reconnect");
}
