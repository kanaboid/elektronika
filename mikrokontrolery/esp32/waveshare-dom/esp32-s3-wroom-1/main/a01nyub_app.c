#include "a01nyub_app.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "a01nyub_app";

static SemaphoreHandle_t s_lock;
static bool s_initialized;
static bool s_valid;
static uint16_t s_distance_mm;
static uint64_t s_last_update_ms;
static bool s_mode_pin_configured;
static bool s_mode_realtime;

static inline uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static bool a01_mode_default_realtime(void)
{
#ifdef CONFIG_A01NYUB_MODE_REALTIME_DEFAULT
    return true;
#else
    return false;
#endif
}

static bool parse_a01nyub_frame(const uint8_t frame[4], uint16_t *out_distance_mm)
{
    if (frame[0] != 0xFF) {
        return false;
    }
    uint8_t checksum = (uint8_t)(frame[0] + frame[1] + frame[2]);
    if (checksum != frame[3]) {
        return false;
    }
    *out_distance_mm = (uint16_t)(((uint16_t)frame[1] << 8) | frame[2]);
    return true;
}

static void a01nyub_task(void *arg)
{
    (void)arg;
    const TickType_t frame_timeout = pdMS_TO_TICKS(CONFIG_A01NYUB_FRAME_TIMEOUT_MS);
    uint8_t rx_buf[64];
    uint8_t frame[4] = {0};
    size_t frame_idx = 0;
    uint16_t last_logged_mm = 0;
    bool have_logged = false;
    uint64_t last_log_ms = 0;

    for (;;) {
        int rd = uart_read_bytes(CONFIG_A01NYUB_UART_PORT, rx_buf, sizeof(rx_buf), frame_timeout);
        if (rd <= 0) {
            continue;
        }

        for (int i = 0; i < rd; i++) {
            uint8_t one = rx_buf[i];
            if (frame_idx == 0) {
                if (one != 0xFF) {
                    continue;
                }
                frame[frame_idx++] = one;
                continue;
            }

            frame[frame_idx++] = one;
            if (frame_idx < sizeof(frame)) {
                continue;
            }

            uint16_t distance_mm = 0;
            if (parse_a01nyub_frame(frame, &distance_mm)) {
                if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
                    s_distance_mm = distance_mm;
                    s_last_update_ms = now_ms();
                    s_valid = true;
                    xSemaphoreGive(s_lock);
                }
                uint64_t t_ms = now_ms();
                bool should_log = !have_logged
                    || (distance_mm > last_logged_mm ? (distance_mm - last_logged_mm) : (last_logged_mm - distance_mm)) >= 20
                    || (t_ms - last_log_ms) >= 1000;
                if (should_log) {
                    ESP_LOGI(TAG, "distance: %u mm", (unsigned)distance_mm);
                    last_logged_mm = distance_mm;
                    last_log_ms = t_ms;
                    have_logged = true;
                }
                frame_idx = 0;
            } else {
                frame_idx = 0;
                if (one == 0xFF) {
                    frame[frame_idx++] = 0xFF;
                }
            }
        }
    }
}

esp_err_t a01nyub_app_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uart_config_t cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(CONFIG_A01NYUB_UART_PORT, 256, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return err;
    }

    err = uart_param_config(CONFIG_A01NYUB_UART_PORT, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        uart_driver_delete(CONFIG_A01NYUB_UART_PORT);
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return err;
    }

    err = uart_set_pin(
        CONFIG_A01NYUB_UART_PORT,
        UART_PIN_NO_CHANGE,
        CONFIG_A01NYUB_UART_RX_GPIO,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        uart_driver_delete(CONFIG_A01NYUB_UART_PORT);
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return err;
    }

    s_initialized = true;
    s_valid = false;
    s_distance_mm = 0;
    s_last_update_ms = 0;
    s_mode_pin_configured = false;
    s_mode_realtime = false;

    int mode_pin = CONFIG_A01NYUB_MODE_GPIO;
    if (mode_pin >= 0 && mode_pin < 64) {
        gpio_config_t mode_cfg = {0};
        mode_cfg.pin_bit_mask = (uint64_t)1ULL << mode_pin;
        mode_cfg.mode = GPIO_MODE_OUTPUT;
        mode_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        mode_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        mode_cfg.intr_type = GPIO_INTR_DISABLE;
        err = gpio_config(&mode_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "mode gpio_config failed: %s", esp_err_to_name(err));
            uart_driver_delete(CONFIG_A01NYUB_UART_PORT);
            vSemaphoreDelete(s_lock);
            s_lock = NULL;
            s_initialized = false;
            return err;
        }
        s_mode_pin_configured = true;
        err = a01nyub_app_set_mode_realtime(a01_mode_default_realtime());
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "set default mode failed: %s", esp_err_to_name(err));
            uart_driver_delete(CONFIG_A01NYUB_UART_PORT);
            vSemaphoreDelete(s_lock);
            s_lock = NULL;
            s_initialized = false;
            return err;
        }
    } else if (mode_pin >= 64) {
        ESP_LOGE(TAG, "invalid mode gpio: %d", mode_pin);
        uart_driver_delete(CONFIG_A01NYUB_UART_PORT);
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        s_initialized = false;
        return ESP_ERR_INVALID_ARG;
    }

    if (xTaskCreate(a01nyub_task, "a01nyub", 4096, NULL, 5, NULL) != pdPASS) {
        uart_driver_delete(CONFIG_A01NYUB_UART_PORT);
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        s_initialized = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "A01NYUB started on UART%d, RX=%d, mode_gpio=%d, 9600 8N1 (TX not used)",
        CONFIG_A01NYUB_UART_PORT,
        CONFIG_A01NYUB_UART_RX_GPIO,
        CONFIG_A01NYUB_MODE_GPIO);
    return ESP_OK;
}

bool a01nyub_app_get_distance_mm(uint16_t *out_mm)
{
    if (out_mm == NULL || !s_initialized || s_lock == NULL) {
        return false;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return false;
    }
    uint64_t age_ms = now_ms() - s_last_update_ms;
    bool fresh = s_valid && age_ms <= CONFIG_A01NYUB_STALE_MS;
    *out_mm = s_distance_mm;
    xSemaphoreGive(s_lock);
    return fresh;
}

a01nyub_status_t a01nyub_app_get_status(void)
{
    a01nyub_status_t st = {
        .initialized = s_initialized,
        .reading_valid = false,
        .reading_stale = true,
        .mode_pin_configured = s_mode_pin_configured,
        .mode_realtime = s_mode_realtime,
        .distance_mm = 0,
        .last_update_ms = 0,
    };
    if (!s_initialized || s_lock == NULL) {
        return st;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return st;
    }
    uint64_t age_ms = now_ms() - s_last_update_ms;
    st.initialized = s_initialized;
    st.distance_mm = s_distance_mm;
    st.last_update_ms = s_last_update_ms;
    st.reading_valid = s_valid;
    st.reading_stale = !s_valid || age_ms > CONFIG_A01NYUB_STALE_MS;
    st.mode_pin_configured = s_mode_pin_configured;
    st.mode_realtime = s_mode_realtime;
    xSemaphoreGive(s_lock);
    return st;
}

esp_err_t a01nyub_app_set_mode_realtime(bool realtime)
{
    if (!s_initialized || s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_mode_pin_configured) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int level = realtime ? 0 : 1;
    esp_err_t err = gpio_set_level(CONFIG_A01NYUB_MODE_GPIO, level);
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_mode_realtime = realtime;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "mode set to %s", realtime ? "realtime (LOW)" : "stable (HIGH)");
    return ESP_OK;
}

bool a01nyub_app_get_mode_realtime(bool *out_realtime)
{
    if (out_realtime == NULL || !s_initialized || s_lock == NULL || !s_mode_pin_configured) {
        return false;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return false;
    }
    *out_realtime = s_mode_realtime;
    xSemaphoreGive(s_lock);
    return true;
}
