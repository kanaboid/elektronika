#include "wifi_app.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_status.h"
#include "nvs_flash.h"

#define WIFI_SSID "SPIDERNET_114"
#define WIFI_PASSWORD "1234554321"
#define WIFI_MAX_RETRY 10

static const char *TAG = "wifi_app";

static SemaphoreHandle_t s_state_lock;
static wifi_status_info_t s_status;
static esp_netif_t *s_sta_netif;
static int s_retry_count;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi start, connecting to %s", WIFI_SSID);
        led_status_set(LED_STATUS_WIFI_CONNECTING);
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_count++;
        led_status_set(LED_STATUS_WIFI_CONNECTING);

        if (s_retry_count <= WIFI_MAX_RETRY) {
            ESP_LOGW(TAG, "Wi-Fi disconnected, retry %d/%d", s_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Wi-Fi disconnected, retry limit reached");
            led_status_set(LED_STATUS_ERROR);
        }

        if (xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_status.connected = false;
            s_status.rssi = 0;
            s_status.ip.addr = 0;
            xSemaphoreGive(s_state_lock);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        wifi_ap_record_t ap_info = {0};
        esp_err_t ap_err = esp_wifi_sta_get_ap_info(&ap_info);

        if (xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_status.connected = true;
            s_status.ip = event->ip_info.ip;
            s_status.rssi = (ap_err == ESP_OK) ? ap_info.rssi : 0;
            strlcpy(s_status.ssid, WIFI_SSID, sizeof(s_status.ssid));
            xSemaphoreGive(s_state_lock);
        }

        s_retry_count = 0;
        led_status_set(LED_STATUS_OK);
        ESP_LOGI(TAG, "Wi-Fi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t wifi_app_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_flash_init failed");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "esp_event_loop_create_default failed");

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        return ESP_FAIL;
    }

    if (s_state_lock == NULL) {
        s_state_lock = xSemaphoreCreateMutex();
        if (s_state_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    memset(&s_status, 0, sizeof(s_status));
    strlcpy(s_status.ssid, WIFI_SSID, sizeof(s_status.ssid));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL), TAG, "register WIFI_EVENT failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL), TAG, "register IP_EVENT failed");

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };

    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    return ESP_OK;
}

esp_netif_t *wifi_app_get_netif(void)
{
    return s_sta_netif;
}

wifi_status_info_t wifi_app_get_status(void)
{
    wifi_status_info_t snapshot = {0};
    if (s_state_lock != NULL && xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        snapshot = s_status;
        xSemaphoreGive(s_state_lock);
    }
    return snapshot;
}
