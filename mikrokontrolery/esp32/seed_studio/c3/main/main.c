/*
 * Seeed XIAO ESP32-C3 — minimal ESP-IDF demo.
 * D10 on the silkscreen is GPIO10 (see wiki pin map); use ~150 Ω in series with an external LED.
 */
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "xiao_c3";

#define BLINK_GPIO GPIO_NUM_10

void app_main(void)
{
    ESP_LOGI(TAG, "Hello from ESP-IDF on XIAO ESP32-C3 (blink GPIO10 / D10)");

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BLINK_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    bool level = false;
    while (1) {
        level = !level;
        ESP_ERROR_CHECK(gpio_set_level(BLINK_GPIO, level));
        ESP_LOGI(TAG, "LED %s", level ? "ON" : "OFF");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
