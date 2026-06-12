// WiFi manager
#pragma once
#include <stdbool.h>
void wifi_init(void);
void wifi_connect(const char *ssid, const char *password);
bool wifi_is_connected(void);
void wifi_disconnect(void);
int8_t wifi_get_rssi(void);
