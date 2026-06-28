#pragma once
#include <stdbool.h>
#include "esp_err.h"

void wifi_init(void);
void wifi_connect(const char *ssid, const char *password);
bool wifi_is_connected(void);
void wifi_disconnect(void);
void wifi_stop_reconnect(void);
int8_t wifi_get_rssi(void);
char *wifi_get_ip(void);

esp_err_t wifi_save_creds(const char *ssid, const char *password);
esp_err_t wifi_load_creds(char *ssid, size_t ssid_len, char *password, size_t pass_len);
bool wifi_has_saved_creds(void);
void wifi_erase_creds(void);
