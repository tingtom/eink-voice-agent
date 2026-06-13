#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "wifi_manager.h"

static const char *TAG = "PROV";

#define PROV_AP_SSID     "EInk-Voice-Config"
#define PROV_AP_CHANNEL  1
#define PROV_AP_MAX_CONN 4

static httpd_handle_t server = NULL;
static bool active = false;

static const char *HTML_PAGE =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>E-Ink Voice Agent Setup</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 20px}"
    "h1{font-size:1.4em;margin-bottom:4px}"
    "p{color:#555;margin-top:0}"
    "label{display:block;margin:16px 0 4px;font-weight:bold}"
    "input[type=text],input[type=password]{width:100%;padding:8px;font-size:1em;box-sizing:border-box}"
    "button{width:100%;padding:12px;margin-top:20px;font-size:1em;background:#0066cc;color:#fff;border:none;border-radius:4px;cursor:pointer}"
    "button:hover{background:#0052a3}"
    "</style>"
    "</head><body>"
    "<h1>E-Ink Voice Agent</h1>"
    "<p>Enter your WiFi credentials</p>"
    "<form id='form'>"
    "<label for='ssid'>WiFi Name (SSID)</label>"
    "<input type='text' id='ssid' placeholder='Network name' required>"
    "<label for='password'>Password</label>"
    "<input type='password' id='password' placeholder='Network password'>"
    "<button type='submit'>Connect</button>"
    "</form>"
    "<div id='msg' style='margin-top:16px;padding:10px;border-radius:4px;display:none'></div>"
    "<script>"
    "document.getElementById('form').onsubmit=async function(e){"
    "e.preventDefault();"
    "var b=document.querySelector('button');b.disabled=true;b.textContent='Connecting...';"
    "var d={ssid:document.getElementById('ssid').value,password:document.getElementById('password').value};"
    "try{"
    "var r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)});"
    "var j=await r.json();"
    "var m=document.getElementById('msg');"
    "m.style.background=j.ok?'#d4edda':'#f8d7da';m.style.color=j.ok?'#155724':'#721c24';"
    "m.textContent=j.message;m.style.display='block';"
    "if(j.ok)setTimeout(function(){document.body.innerHTML='<h1>Success!</h1><p>Rebooting...</p>'},1500);"
    "}catch(e){"
    "var m=document.getElementById('msg');"
    "m.style.background='#f8d7da';m.style.color='#721c24';m.textContent='Connection failed';m.style.display='block';"
    "}"
    "b.disabled=false;b.textContent='Connect';"
    "}"
    "</script>"
    "</body></html>";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_PAGE, strlen(HTML_PAGE));
    return ESP_OK;
}

static void json_parse_str(const char *json, const char *key, char *out, size_t out_len)
{
    const char *p = strstr(json, key);
    if (!p) return;
    p = strchr(p, ':');
    if (!p) return;
    p++;
    while (*p == ' ' || *p == '"') p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) out[i++] = *p++;
    out[i] = '\0';
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    char ssid[64] = {0};
    char password[64] = {0};
    json_parse_str(buf, "\"ssid\"", ssid, sizeof(ssid));
    json_parse_str(buf, "\"password\"", password, sizeof(password));

    if (strlen(ssid) == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"message\":\"SSID is required\"}");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Received credentials for SSID: %s", ssid);

    esp_err_t err = wifi_save_creds(ssid, password);
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"message\":\"Failed to save\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"Saved! Rebooting...\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t provisioning_start_ap(void)
{
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    if (!ap) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return ESP_FAIL;
    }

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = PROV_AP_SSID,
            .ssid_len = strlen(PROV_AP_SSID),
            .channel = PROV_AP_CHANNEL,
            .max_connection = PROV_AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP '%s' started at 192.168.4.1", PROV_AP_SSID);
    return ESP_OK;
}

esp_err_t provisioning_start_server(void)
{
    httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_cfg.max_uri_handlers = 5;

    if (httpd_start(&server, &httpd_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    httpd_uri_t config_uri = { .uri = "/config", .method = HTTP_POST, .handler = config_post_handler };

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &config_uri);

    active = true;
    ESP_LOGI(TAG, "Server at http://192.168.4.1");
    return ESP_OK;
}

void provisioning_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
    active = false;
}

bool provisioning_is_active(void)
{
    return active;
}
