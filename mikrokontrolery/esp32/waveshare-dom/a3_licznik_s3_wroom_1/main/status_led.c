#include "status_led.h"

#include <string.h>

#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip_encoder.h"

static const char *TAG = "status_led";

#define RMT_RESOLUTION_HZ  10000000

static rmt_channel_handle_t s_rmt_chan;
static rmt_encoder_handle_t s_encoder;
static uint8_t s_pixel[3];
static bool s_ready;
static SemaphoreHandle_t s_lock;

static void pixel_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    /* WS2812: kolejność GRB */
    s_pixel[0] = g;
    s_pixel[1] = b;
    s_pixel[2] = r;
}

static esp_err_t pixel_flush(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    rmt_encoder_reset(s_encoder);

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };
    esp_err_t err = rmt_transmit(s_rmt_chan, s_encoder, s_pixel, sizeof(s_pixel), &tx_cfg);
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(s_rmt_chan, pdMS_TO_TICKS(500));
    }
    return err;
}

esp_err_t status_led_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = STATUS_LED_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &s_rmt_chan);
    if (err != ESP_OK) {
        return err;
    }

    led_strip_encoder_config_t enc_cfg = {
        .resolution = RMT_RESOLUTION_HZ,
    };
    err = rmt_new_led_strip_encoder(&enc_cfg, &s_encoder);
    if (err != ESP_OK) {
        return err;
    }

    err = rmt_enable(s_rmt_chan);
    if (err != ESP_OK) {
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_ready = true;
    status_led_set(STATUS_LED_OFF);
    ESP_LOGI(TAG, "NeoPixel on GPIO%d ready", STATUS_LED_GPIO);
    return ESP_OK;
}

void status_led_set(status_led_state_t state)
{
    switch (state) {
    case STATUS_LED_BOOT:
        pixel_rgb(0, 0, 64);
        break;
    case STATUS_LED_READY:
        pixel_rgb(0, 48, 0);
        break;
    case STATUS_LED_SWEEP_UP:
        pixel_rgb(0, 80, 0);
        break;
    case STATUS_LED_SWEEP_DOWN:
        pixel_rgb(200, 40, 0);
        break;
    case STATUS_LED_IDLE:
        pixel_rgb(0, 32, 48);
        break;
    case STATUS_LED_ERROR:
        pixel_rgb(80, 0, 0);
        break;
    case STATUS_LED_OFF:
    default:
        pixel_rgb(0, 0, 0);
        break;
    }

    if (!s_ready || s_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    esp_err_t err = pixel_flush();
    xSemaphoreGive(s_lock);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "flush failed: %s", esp_err_to_name(err));
    }
}

void status_led_boot_sequence(void)
{
    for (int i = 0; i < 3; i++) {
        status_led_set(STATUS_LED_BOOT);
        vTaskDelay(pdMS_TO_TICKS(120));
        status_led_set(STATUS_LED_OFF);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    status_led_set(STATUS_LED_READY);
    vTaskDelay(pdMS_TO_TICKS(400));
}
