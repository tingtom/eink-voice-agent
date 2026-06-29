#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "wifi_manager.h"

static const char *TAG = "WIFI";
static const char *NVS_NS = "wifi";
static const char *NVS_KEY_SSID = "ssid";
static const char *NVS_KEY_PASS = "password";

static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static int8_t current_rssi = -100;
static bool connected = false;
static char current_ip[16] = {0};
static bool wifi_reconnect_allowed = true;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        connected = false;
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        memset(current_ip, 0, sizeof(current_ip));
        wifi_event_sta_disconnected_t *discon = (wifi_event_sta_disconnected_t *)data;
        if (wifi_reconnect_allowed) {
            ESP_LOGW(TAG, "WiFi disconnected (reason %d), reconnecting...", discon->reason);
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "WiFi disconnected (reason %d) — reconnects disabled", discon->reason);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        connected = true;
        current_rssi = 0;
        snprintf(current_ip, sizeof(current_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", current_ip);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.ampdu_tx_enable = false;
    cfg.ampdu_rx_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    ESP_LOGI(TAG, "WiFi initialized");
}
void wifi_connect(const char *ssid, const char *password)
{
    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold = {
                .authmode = WIFI_AUTH_WPA2_PSK,
            },
            .pmf_cfg = {
                .capable = false,
                .required = false,
            },
        },
    };
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi...");
}

bool wifi_is_connected(void)
{
    return connected;
}

void wifi_disconnect(void)
{
    esp_wifi_disconnect();
    connected = false;
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
}

void wifi_stop_reconnect(void)
{
    wifi_reconnect_allowed = false;
    esp_wifi_stop();
}

int8_t wifi_get_rssi(void)
{
    if (!connected) return -100;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        current_rssi = ap.rssi;
    }
    return current_rssi;
}

char *wifi_get_ip(void)
{
    return current_ip;
}

esp_err_t wifi_save_creds(const char *ssid, const char *password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_PASS, password);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t wifi_load_creds(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t len = ssid_len;
    err = nvs_get_str(h, NVS_KEY_SSID, ssid, &len);
    if (err != ESP_OK) { nvs_close(h); return err; }

    len = pass_len;
    err = nvs_get_str(h, NVS_KEY_PASS, password, &len);
    nvs_close(h);
    return err;
}

bool wifi_has_saved_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 0;
    esp_err_t err = nvs_get_str(h, NVS_KEY_SSID, NULL, &len);
    nvs_close(h);
    return err == ESP_OK;
}

void wifi_erase_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "WiFi credentials erased");
    }
}
