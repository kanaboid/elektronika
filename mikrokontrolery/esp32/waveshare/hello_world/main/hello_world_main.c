#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "led_strip.h"
#include "nvs_flash.h"

#define WIFI_SSID "Rrr a52s"
#define WIFI_PASSWORD "1234554321"

#define CONTROL_PIN GPIO_NUM_6
#define RGB_BUILTIN_GPIO GPIO_NUM_10
#define RGB_BRIGHTNESS 10
#define STATUS_INTERVAL_MS 5000

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_CONNECT_TIMEOUT_MS 15000

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint32_t duration_ms;
} led_step_t;

static const char *TAG = "blinkrgb_idf";
static EventGroupHandle_t s_wifi_event_group;
static bool s_control_pin_high = false;
static led_strip_handle_t s_led_strip;

static const led_step_t s_led_sequence[] = {
    {RGB_BRIGHTNESS, RGB_BRIGHTNESS, RGB_BRIGHTNESS, 1000},
    {0, 0, 0, 1000},
    {RGB_BRIGHTNESS, 0, 0, 1000},
    {0, RGB_BRIGHTNESS, 0, 3000},
    {0, 0, RGB_BRIGHTNESS, 1000},
    {0, 0, 0, 1000},
};

static esp_err_t set_control_pin(bool high)
{
    s_control_pin_high = high;
    return gpio_set_level(CONTROL_PIN, high ? 1 : 0);
}

static esp_err_t pin6_on_handler(httpd_req_t *req)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_control_pin(true));
    ESP_LOGI(TAG, "Web request: PIN 6 ON");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t pin6_off_handler(httpd_req_t *req)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(set_control_pin(false));
    ESP_LOGI(TAG, "Web request: PIN 6 OFF");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    const httpd_uri_t pin6_on_uri = {
        .uri = "/pin6/on",
        .method = HTTP_GET,
        .handler = pin6_on_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t pin6_off_uri = {
        .uri = "/pin6/off",
        .method = HTTP_GET,
        .handler = pin6_off_handler,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server, &pin6_on_uri));
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server, &pin6_off_uri));
    ESP_LOGI(TAG, "HTTP server started. Use /pin6/on or /pin6/off");
    return server;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected. IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = { 0 };
    memcpy(wifi_config.sta.ssid, WIFI_SSID, sizeof(WIFI_SSID));
    memcpy(wifi_config.sta.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected");
    } else {
        ESP_LOGW(TAG, "Initial WiFi connection timeout, reconnect will continue in background");
    }
}

static void led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_BUILTIN_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip));
}

static void led_apply_color(uint8_t r, uint8_t g, uint8_t b)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_set_pixel(s_led_strip, 0, r, g, b));
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_refresh(s_led_strip));
}

void app_main(void)
{
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << CONTROL_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(set_control_pin(false));

    led_init();
    wifi_init_sta();
    start_webserver();

    const size_t led_step_count = sizeof(s_led_sequence) / sizeof(s_led_sequence[0]);
    size_t current_led_step = 0;
    TickType_t step_start = xTaskGetTickCount();
    TickType_t next_status = step_start + pdMS_TO_TICKS(STATUS_INTERVAL_MS);

    led_apply_color(
        s_led_sequence[current_led_step].r,
        s_led_sequence[current_led_step].g,
        s_led_sequence[current_led_step].b);

    while (1) {
        TickType_t now = xTaskGetTickCount();
        if ((now - step_start) >= pdMS_TO_TICKS(s_led_sequence[current_led_step].duration_ms)) {
            current_led_step = (current_led_step + 1) % led_step_count;
            step_start = now;
            led_apply_color(
                s_led_sequence[current_led_step].r,
                s_led_sequence[current_led_step].g,
                s_led_sequence[current_led_step].b);
        }

        if (now >= next_status) {
            next_status = now + pdMS_TO_TICKS(STATUS_INTERVAL_MS);
            wifi_ap_record_t ap_info;
            esp_err_t wifi_state = esp_wifi_sta_get_ap_info(&ap_info);
            ESP_LOGI(TAG, "Pin 6: %s", s_control_pin_high ? "HIGH" : "LOW");
            ESP_LOGI(TAG, "WiFi status: %s", wifi_state == ESP_OK ? "CONNECTED" : "DISCONNECTED");
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
