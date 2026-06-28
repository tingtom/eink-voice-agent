#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "app_config.h"

static const char *TAG = "HTTP";

typedef struct {
    char *buf;
    size_t len;
} http_resp_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data) {
        http_resp_t *ctx = (http_resp_t *)evt->user_data;
        if (ctx->buf && ctx->len > 0) {
            size_t copy_len = evt->data_len;
            if (copy_len > ctx->len) copy_len = ctx->len;
            memcpy(ctx->buf, evt->data, copy_len);
            ctx->buf += copy_len;
            ctx->len -= copy_len;
        }
    }
    return ESP_OK;
}

esp_err_t http_post_json(const char *url, const char *json, char *response, size_t response_len)
{
    http_resp_t resp_ctx = {.buf = response, .len = response_len};
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx,
        .timeout_ms = 5000,
        .skip_cert_common_name_check = true,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-Device-ID", DEVICE_ID);
    esp_http_client_set_header(client, "X-Device-Token", DEVICE_AUTH_TOKEN);
    esp_http_client_set_header(client, "Connection", "close");
    esp_http_client_set_post_field(client, json, strlen(json));

    ESP_LOGD(TAG, "POST %s json_len=%u", url, (unsigned)strlen(json));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "POST %s status=%d", url, esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "POST %s failed: %s (errno=%d)", url, esp_err_to_name(err), err);
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t http_get_json(const char *url, char *response, size_t response_len)
{
    http_resp_t resp_ctx = {.buf = response, .len = response_len};
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx,
        .timeout_ms = 5000,
        .skip_cert_common_name_check = true,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "X-Device-ID", DEVICE_ID);
    esp_http_client_set_header(client, "X-Device-Token", DEVICE_AUTH_TOKEN);
    esp_http_client_set_header(client, "Connection", "close");
    ESP_LOGD(TAG, "GET %s", url);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "GET %s status=%d", url, esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "GET %s failed: %s (errno=%d)", url, esp_err_to_name(err), err);
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t http_post_binary(const char *url, const uint8_t *data, size_t len, char *response, size_t response_len)
{
    http_resp_t resp_ctx = {.buf = response, .len = response_len};
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx,
        .timeout_ms = 5000,
        .skip_cert_common_name_check = true,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(client, "X-Device-ID", DEVICE_ID);
    esp_http_client_set_header(client, "X-Device-Token", DEVICE_AUTH_TOKEN);
    esp_http_client_set_header(client, "Connection", "close");
    ESP_LOGD(TAG, "POST %s len=%u", url, (unsigned)len);
    esp_http_client_set_post_field(client, (const char *)data, len);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "POST %s status=%d len=%u", url, esp_http_client_get_status_code(client), (unsigned)len);
    } else {
        ESP_LOGE(TAG, "POST %s failed: %s (errno=%d) len=%u", url, esp_err_to_name(err), err, (unsigned)len);
    }

    esp_http_client_cleanup(client);
    return err;
}
