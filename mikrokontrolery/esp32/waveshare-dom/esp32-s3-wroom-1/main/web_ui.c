#include "web_ui.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "a01nyub_app.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "ds18b20_app.h"
#include "wifi_app.h"

static const char *TAG = "web_ui";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    wifi_status_info_t status = wifi_app_get_status();
    a01nyub_status_t a01 = a01nyub_app_get_status();

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

    char a01_text[128];
    const char *a01_mode_text = "mode pin: n/a";
    if (!a01.initialized) {
        snprintf(a01_text, sizeof(a01_text), "brak inicjalizacji");
    } else if (!a01.reading_valid) {
        snprintf(a01_text, sizeof(a01_text), "brak poprawnej ramki");
    } else if (a01.reading_stale) {
        snprintf(a01_text, sizeof(a01_text), "%u mm (stary odczyt)", (unsigned)a01.distance_mm);
    } else {
        snprintf(a01_text, sizeof(a01_text), "%u mm", (unsigned)a01.distance_mm);
    }
    if (a01.mode_pin_configured) {
        a01_mode_text = a01.mode_realtime ? "mode: realtime (LOW)" : "mode: stable (HIGH)";
    }

    char html[3200];
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
        "<dt>A01NYUB (UART)</dt><dd>%s<br/>%s<br/>"
        "<a href=\"/api/a01nyub/mode?realtime=1\">Ustaw realtime (LOW)</a> | "
        "<a href=\"/api/a01nyub/mode?realtime=0\">Ustaw stable (HIGH)</a></dd>"
        "<dt>DS18B20 (1-Wire)</dt><dd>%s</dd>"
        "<dt>SSID</dt><dd>%s</dd>"
        "<dt>IP</dt><dd>%s</dd>"
        "<dt>RSSI</dt><dd>%d dBm</dd>"
        "</dl><p>Odswiez strone, aby zobaczyc aktualny stan. Adresy ROM 64b — potrzebne przy wielu czujnikach na jednej lini.</p></div></body></html>",
        status.connected ? "ok" : "bad",
        state_text,
        a01_text,
        a01_mode_text,
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

static esp_err_t a01_mode_get_handler(httpd_req_t *req)
{
    char query[64];
    bool has_realtime = false;
    bool realtime = false;

    if (httpd_req_get_url_query_len(req) > 0) {
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            char val[8];
            if (httpd_query_key_value(query, "realtime", val, sizeof(val)) == ESP_OK) {
                has_realtime = true;
                realtime = (strcmp(val, "1") == 0) || (strcmp(val, "true") == 0) || (strcmp(val, "on") == 0);
            }
        }
    }

    esp_err_t set_err = ESP_OK;
    if (has_realtime) {
        set_err = a01nyub_app_set_mode_realtime(realtime);
    }

    a01nyub_status_t a01 = a01nyub_app_get_status();
    char json[256];
    int len = snprintf(
        json,
        sizeof(json),
        "{\"ok\":%s,\"mode_pin_configured\":%s,\"mode_realtime\":%s,\"set_result\":\"%s\"}",
        (set_err == ESP_OK || !has_realtime) ? "true" : "false",
        a01.mode_pin_configured ? "true" : "false",
        a01.mode_realtime ? "true" : "false",
        has_realtime ? esp_err_to_name(set_err) : "UNCHANGED");
    if (len < 0 || len >= (int)sizeof(json)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json buffer overflow");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t sensors_get_handler(httpd_req_t *req)
{
    wifi_status_info_t wifi = wifi_app_get_status();
    a01nyub_status_t a01 = a01nyub_app_get_status();
    ds18b20_app_sensor_t ds[DS18B20_APP_MAX_SENSORS];
    size_t ds_count = ds18b20_app_get_sensors_copy(ds, DS18B20_APP_MAX_SENSORS);
    const char *ip = wifi.connected ? ip4addr_ntoa((const ip4_addr_t *)&wifi.ip) : "0.0.0.0";
    char ds_json[1200];
    size_t off = 0;
    ds_json[0] = '\0';
    for (size_t i = 0; i < ds_count; i++) {
        int w = snprintf(
            ds_json + off,
            sizeof(ds_json) - off,
            "%s{\"address_hex\":\"%s\",\"address_u64\":\"0x%016" PRIx64 "\",\"reading_valid\":%s,\"temperature_c\":%.2f}",
            (i == 0) ? "" : ",",
            ds[i].address_hex[0] ? ds[i].address_hex : "",
            (uint64_t)ds[i].address,
            ds[i].reading_valid ? "true" : "false",
            ds[i].temperature_c);
        if (w < 0) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json encode failed");
        }
        off += (size_t)w;
        if (off >= sizeof(ds_json)) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json ds buffer overflow");
        }
    }

    char json[2200];
    int len = snprintf(
        json,
        sizeof(json),
        "{\"wifi\":{\"connected\":%s,\"ssid\":\"%s\",\"rssi_dbm\":%d,\"ip\":\"%s\"},"
        "\"a01nyub\":{\"initialized\":%s,\"reading_valid\":%s,\"reading_stale\":%s,\"mode_pin_configured\":%s,\"mode_realtime\":%s,\"distance_mm\":%u,\"last_update_ms\":%" PRIu64 "},"
        "\"ds18b20\":[%s]}",
        wifi.connected ? "true" : "false",
        wifi.ssid[0] != '\0' ? wifi.ssid : "",
        wifi.rssi,
        ip,
        a01.initialized ? "true" : "false",
        a01.reading_valid ? "true" : "false",
        a01.reading_stale ? "true" : "false",
        a01.mode_pin_configured ? "true" : "false",
        a01.mode_realtime ? "true" : "false",
        (unsigned)a01.distance_mm,
        a01.last_update_ms,
        ds_json);
    if (len < 0 || len >= (int)sizeof(json)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json buffer overflow");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
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
    httpd_uri_t sensors = {
        .uri = "/api/sensors",
        .method = HTTP_GET,
        .handler = sensors_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t a01_mode = {
        .uri = "/api/a01nyub/mode",
        .method = HTTP_GET,
        .handler = a01_mode_get_handler,
        .user_ctx = NULL,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &root), TAG, "register / failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &sensors), TAG, "register /api/sensors failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &a01_mode), TAG, "register /api/a01nyub/mode failed");
    ESP_LOGI(TAG, "Web UI started on port %d", config.server_port);
    return ESP_OK;
}
