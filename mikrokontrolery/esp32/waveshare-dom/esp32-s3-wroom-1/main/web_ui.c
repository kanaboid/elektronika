#include "web_ui.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "ds18b20_app.h"
#include "wifi_app.h"

static const char *TAG = "web_ui";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    wifi_status_info_t status = wifi_app_get_status();

    const char *state_text = status.connected ? "CONNECTED" : "DISCONNECTED";
    const char *ip_text = status.connected ? ip4addr_ntoa((const ip4_addr_t *)&status.ip) : "0.0.0.0";

    ds18b20_app_sensor_t s[DS18B20_APP_MAX_SENSORS];
    size_t n = ds18b20_app_get_sensors_copy(s, DS18B20_APP_MAX_SENSORS);

    char ds_block[1200];
    size_t off = 0;
    if (n == 0) {
        snprintf(ds_block, sizeof(ds_block), "%s", "— (brak czujnikow lub inicjacji 1-Wire)");
    } else {
        for (size_t i = 0; i < n; i++) {
            char tbuf[40];
            if (s[i].reading_valid) {
                snprintf(tbuf, sizeof(tbuf), "%.2f °C", s[i].temperature_c);
            } else {
                snprintf(tbuf, sizeof(tbuf), "brak pomiaru");
            }
            size_t rem = sizeof(ds_block) - off;
            if (rem < 1) {
                break;
            }
            int w = snprintf(
                ds_block + off,
                rem,
                "%s<span class='mono'>%s</span> — <strong>%s</strong>"
                " <span class='rom'>(0x%016" PRIx64 ")</span>",
                (i == 0) ? "" : "<br/>",
                s[i].address_hex[0] ? s[i].address_hex : "?",
                tbuf,
                (uint64_t)s[i].address);
            if (w < 0) {
                break;
            }
            off += (size_t)w;
            if (off >= sizeof(ds_block)) {
                off = sizeof(ds_block) - 1;
                break;
            }
        }
    }

    char html[2200];
    int len = snprintf(
        html,
        sizeof(html),
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32 Wi-Fi Status</title>"
        "<style>body{font-family:Arial,sans-serif;background:#0f172a;color:#e2e8f0;padding:24px}"
        ".card{max-width:640px;background:#1e293b;padding:20px;border-radius:12px}"
        "h1{margin-top:0}.ok{color:#22c55e}.bad{color:#ef4444}dt{font-weight:bold}dd{margin:0 0 10px}"
        ".mono{font-family:Consolas,ui-monospace,monospace;letter-spacing:0.02em}"
        ".rom{opacity:0.75;font-size:0.85em;white-space:nowrap}</style>"
        "</head><body><div class=\"card\"><h1>ESP32 Wi-Fi Info</h1>"
        "<p>Status: <strong class=\"%s\">%s</strong></p><dl>"
        "<dt>DS18B20 (1-Wire)</dt><dd>%s</dd>"
        "<dt>SSID</dt><dd>%s</dd>"
        "<dt>IP</dt><dd>%s</dd>"
        "<dt>RSSI</dt><dd>%d dBm</dd>"
        "</dl><p>Odswiez strone, aby zobaczyc aktualny stan. Adresy ROM 64b — potrzebne przy wielu czujnikach na jednej lini.</p></div></body></html>",
        status.connected ? "ok" : "bad",
        state_text,
        ds_block,
        status.ssid[0] != '\0' ? status.ssid : "-",
        ip_text,
        status.rssi);

    if (len < 0 || len >= (int)sizeof(html)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "HTML buffer overflow");
    }

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t web_ui_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    /* Default 4096 B is too small: root_get_handler() uses ~3.4 kB of locals alone. */
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "httpd_start failed");

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &root), TAG, "register / failed");
    ESP_LOGI(TAG, "Web UI started on port %d", config.server_port);
    return ESP_OK;
}
