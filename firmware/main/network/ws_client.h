// WebSocket client
#pragma once
#include "esp_err.h"
typedef void (*ws_message_callback_t)(const char *data, size_t len);
esp_err_t ws_client_init(const char *url, const char *token);
esp_err_t ws_client_send_audio(const uint8_t *data, size_t len);
esp_err_t ws_client_send_text(const char *text);
void ws_client_set_callback(ws_message_callback_t cb);
bool ws_client_is_connected(void);
