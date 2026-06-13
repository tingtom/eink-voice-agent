#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "app_config.h"

static const char *TAG = "HTTP";

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data) {
        size_t *written = (size_t *)evt->user_data;
        char **resp = (char **)evt->user_data;
        (void)written;
        if (*resp) {
            size_t copy_len = evt->data_len;
            if (copy_len > 0) {
                memcpy(*resp, evt->data, copy_len);
                *resp += copy_len;
            }
        }
    }
    return ESP_OK;
}

esp_err_t http_post_json(const char *url, const char *json, char *response, size_t response_len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = response,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, strlen(json));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "POST %s status=%d", url, esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "POST %s failed: %s", url, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t http_get_json(const char *url, char *response, size_t response_len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = response,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "GET %s status=%d", url, esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "GET %s failed: %s", url, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}
