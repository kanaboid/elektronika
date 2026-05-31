#include "wifi_time.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "wifi_time";

static bool s_sta_connected;
static bool s_ntp_synced;

#if CONFIG_A3_WIFI_ENABLE

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT  BIT0

static EventGroupHandle_t s_wifi_events;

static void on_sntp_sync(struct timeval *tv)
{
    (void)tv;
    s_ntp_synced = true;

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    ESP_LOGI(TAG, "NTP OK: %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
}

static void sntp_start(void)
{
    if (esp_sntp_enabled()) {
        return;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_A3_NTP_SERVER);
    esp_sntp_set_time_sync_notification_cb(on_sntp_sync);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP: %s", CONFIG_A3_NTP_SERVER);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_connected = false;
        if (s_wifi_events != NULL) {
            xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        }
        ESP_LOGW(TAG, "WiFi rozlaczone — ponawiam...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_sta_connected = true;
        if (s_wifi_events != NULL) {
            xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        }
        sntp_start();
    }
}

static esp_err_t nvs_init_once(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t wifi_time_init(void)
{
    if (CONFIG_A3_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "SSID pusty — ustaw: idf.py menuconfig -> A3 Cluster — siec i czas");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_init_once();
    if (err != ESP_OK) {
        return err;
    }

    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = { 0 };
    strncpy((char *)wifi_cfg.sta.ssid, CONFIG_A3_WIFI_SSID, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, CONFIG_A3_WIFI_PASSWORD,
            sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode =
        (CONFIG_A3_WIFI_PASSWORD[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi laczenie z SSID: %s", CONFIG_A3_WIFI_SSID);
    return ESP_OK;
}

#else

esp_err_t wifi_time_init(void)
{
    ESP_LOGI(TAG, "WiFi wylaczone (CONFIG_A3_WIFI_ENABLE=n)");
    return ESP_OK;
}

#endif /* CONFIG_A3_WIFI_ENABLE */

bool wifi_time_sta_connected(void)
{
    return s_sta_connected;
}

bool wifi_time_ntp_synced(void)
{
    return s_ntp_synced;
}

void wifi_time_print_status(void)
{
#if CONFIG_A3_WIFI_ENABLE
    printf("WiFi: %s", CONFIG_A3_WIFI_SSID);
    if (CONFIG_A3_WIFI_SSID[0] == '\0') {
        printf(" (SSID nie ustawiony w menuconfig)\n");
        return;
    }
    printf(" — %s", s_sta_connected ? "polaczony" : "laczenie...");
    printf(" | NTP (%s): %s\n", CONFIG_A3_NTP_SERVER,
           s_ntp_synced ? "zsynchronizowany" : "czekam...");
    if (s_ntp_synced) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        printf("Czas: %04d-%02d-%02d %02d:%02d:%02d\n",
               t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
               t.tm_hour, t.tm_min, t.tm_sec);
    }
#else
    printf("WiFi wylaczone w menuconfig\n");
#endif
}
