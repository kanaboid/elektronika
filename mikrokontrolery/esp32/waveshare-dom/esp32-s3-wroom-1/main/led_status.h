#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

typedef enum {
    LED_STATUS_OFF = 0,
    LED_STATUS_BOOT,
    LED_STATUS_WIFI_CONNECTING,
    LED_STATUS_OK,
    LED_STATUS_ERROR,
} led_status_t;

esp_err_t led_status_init(gpio_num_t gpio, uint32_t led_count);
esp_err_t led_status_set(led_status_t status);
