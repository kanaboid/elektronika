#include "led_status.h"

#include "esp_check.h"
#include "led_strip.h"

static led_strip_handle_t s_strip = NULL;

static esp_err_t led_status_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    if (s_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(led_strip_set_pixel(s_strip, 0, red, green, blue), "led_status", "set pixel failed");
    return led_strip_refresh(s_strip);
}

esp_err_t led_status_init(gpio_num_t gpio, uint32_t led_count)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio,
        .max_leds = led_count,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };

    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip), "led_status", "init strip failed");
    return led_strip_clear(s_strip);
}

esp_err_t led_status_set(led_status_t status)
{
    switch (status) {
        case LED_STATUS_BOOT:
            return led_status_set_rgb(0, 0, 16);
        case LED_STATUS_WIFI_CONNECTING:
            return led_status_set_rgb(16, 16, 0);
        case LED_STATUS_OK:
            return led_status_set_rgb(0, 16, 0);
        case LED_STATUS_ERROR:
            return led_status_set_rgb(16, 0, 0);
        case LED_STATUS_OFF:
        default:
            if (s_strip == NULL) {
                return ESP_ERR_INVALID_STATE;
            }
            return led_strip_clear(s_strip);
    }
}
