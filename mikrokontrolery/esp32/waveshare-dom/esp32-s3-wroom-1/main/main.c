#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "a01nyub_app.h"
#include "ds18b20_app.h"
#include "led_status.h"
#include "web_ui.h"
#include "wifi_app.h"

#define RGB_LED_GPIO GPIO_NUM_38
#define RGB_LED_COUNT 1

static const char *TAG = "iot_base";

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing LED status module on GPIO38");
    ESP_ERROR_CHECK(led_status_init(RGB_LED_GPIO, RGB_LED_COUNT));
    ESP_ERROR_CHECK(led_status_set(LED_STATUS_BOOT));

    if (ds18b20_app_start() != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20 init failed (wiring on configured GPIO or pull-up?)");
    }

    if (a01nyub_app_start() != ESP_OK) {
        ESP_LOGW(TAG, "A01NYUB init failed (check UART wiring/pins)");
    }

    ESP_LOGI(TAG, "Starting Wi-Fi station");
    ESP_ERROR_CHECK(wifi_app_start());

    ESP_LOGI(TAG, "Starting web UI");
    ESP_ERROR_CHECK(web_ui_start());

    ESP_LOGI(TAG, "System ready");
}