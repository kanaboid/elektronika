#include "freertos/FreeRTOS.h"
#include "esp_log.h"
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

    ESP_LOGI(TAG, "Starting Wi-Fi station");
    ESP_ERROR_CHECK(wifi_app_start());

    ESP_LOGI(TAG, "Starting web UI");
    ESP_ERROR_CHECK(web_ui_start());

    ESP_LOGI(TAG, "System ready");
}