#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"
#include "lwip/ip_addr.h"

typedef struct {
    bool connected;
    char ssid[33];
    int8_t rssi;
    esp_ip4_addr_t ip;
} wifi_status_info_t;

esp_err_t wifi_app_start(void);
esp_netif_t *wifi_app_get_netif(void);
wifi_status_info_t wifi_app_get_status(void);
