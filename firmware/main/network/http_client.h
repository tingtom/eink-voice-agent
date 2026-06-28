// HTTP client
#pragma once
#include "esp_err.h"
esp_err_t http_post_json(const char *url, const char *json, char *response, size_t response_len);
esp_err_t http_get_json(const char *url, char *response, size_t response_len);
esp_err_t http_post_binary(const char *url, const uint8_t *data, size_t len, char *response, size_t response_len);
