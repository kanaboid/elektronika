#include "ds18b20_app.h"

#include <inttypes.h>
#include <string.h>

#include "onewire_bus.h"
#include "onewire_device.h"
#include "ds18b20.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "ds18b20_app";

static onewire_bus_handle_t s_bus;
static ds18b20_device_handle_t s_dev[DS18B20_APP_MAX_SENSORS];
static size_t s_count;
static SemaphoreHandle_t s_lock;
static ds18b20_app_sensor_t s_sensors[DS18B20_APP_MAX_SENSORS];
static bool s_inited;

static void format_rom_u64_le(uint64_t rom, char *out, size_t n)
{
    uint8_t b[8];
    memcpy(b, &rom, sizeof(rom));
    snprintf(out, n, "%02x%02x%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
}

static void ds18b20_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_inited && s_count > 0) {
            esp_err_t e = ds18b20_trigger_temperature_conversion_for_all(s_bus);
            if (e == ESP_OK) {
                if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
                    for (size_t i = 0; i < s_count; i++) {
                        float t;
                        e = ds18b20_get_temperature(s_dev[i], &t);
                        if (e == ESP_OK) {
                            s_sensors[i].temperature_c = t;
                            s_sensors[i].reading_valid = true;
                        } else {
                            s_sensors[i].reading_valid = false;
                            ESP_LOGW(
                                TAG,
                                "[%s] get_temperature %s",
                                s_sensors[i].address_hex[0] ? s_sensors[i].address_hex : "?",
                                esp_err_to_name(e));
                        }
                    }
                    for (size_t i = 0; i < s_count; i++) {
                        if (s_sensors[i].reading_valid) {
                            ESP_LOGI(
                                TAG,
                                "%s  T = %.2f C",
                                s_sensors[i].address_hex,
                                s_sensors[i].temperature_c);
                        }
                    }
                    xSemaphoreGive(s_lock);
                }
            } else {
                ESP_LOGW(TAG, "trigger (all): %s", esp_err_to_name(e));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t ds18b20_app_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    onewire_bus_config_t bus_config = {
        .bus_gpio_num = CONFIG_DS18B20_GPIO_NUM,
        .flags = { .en_pull_up = 0 },
    };
    onewire_bus_rmt_config_t rmt_config = { .max_rx_bytes = 32 };

    esp_err_t err = onewire_new_bus_rmt(&bus_config, &rmt_config, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "onewire_new_bus_rmt: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return err;
    }

    onewire_device_iter_handle_t it = NULL;
    err = onewire_new_device_iter(s_bus, &it);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "onewire_new_device_iter: %s", esp_err_to_name(err));
        onewire_bus_del(s_bus);
        s_bus = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return err;
    }

    ds18b20_config_t ds_config = { };
    onewire_device_t odev;

    for (;;) {
        err = onewire_device_iter_get_next(it, &odev);
        if (err == ESP_ERR_NOT_FOUND) {
            break;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "onewire_device_iter_get_next: %s", esp_err_to_name(err));
            onewire_del_device_iter(it);
            onewire_bus_del(s_bus);
            s_bus = NULL;
            vSemaphoreDelete(s_lock);
            s_lock = NULL;
            return err;
        }

        ds18b20_device_handle_t dh = NULL;
        err = ds18b20_new_device_from_enumeration(&odev, &ds_config, &dh);
        if (err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGD(TAG, "skip non-DS18B20, ROM=0x%016" PRIx64, (uint64_t)odev.address);
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ds18b20_new_device_from_enumeration: %s", esp_err_to_name(err));
            continue;
        }

        if (s_count >= DS18B20_APP_MAX_SENSORS) {
            ESP_LOGW(TAG, "max %d DS18B20 — ignoring further devices", DS18B20_APP_MAX_SENSORS);
            ds18b20_del_device(dh);
            break;
        }

        onewire_device_address_t addr;
        if (ds18b20_get_device_address(dh, &addr) == ESP_OK) {
            s_sensors[s_count].address = (uint64_t)addr;
            format_rom_u64_le(s_sensors[s_count].address, s_sensors[s_count].address_hex, sizeof(s_sensors[0].address_hex));
        } else {
            s_sensors[s_count].address = 0;
            s_sensors[s_count].address_hex[0] = '\0';
        }
        s_sensors[s_count].reading_valid = false;
        s_dev[s_count] = dh;
        ESP_LOGI(
            TAG,
            "DS18B20 found: 0x%s (64-bit: 0x%016" PRIx64 ")",
            s_sensors[s_count].address_hex,
            s_sensors[s_count].address);
        s_count++;
    }

    onewire_del_device_iter(it);

    if (s_count == 0) {
        ESP_LOGE(TAG, "no DS18B20 on bus");
        onewire_bus_del(s_bus);
        s_bus = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < s_count; i++) {
        if (ds18b20_set_resolution(s_dev[i], DS18B20_RESOLUTION_12B) != ESP_OK) {
            ESP_LOGW(TAG, "ds18b20_set_resolution failed for %s", s_sensors[i].address_hex);
        }
    }

    s_inited = true;

    if (xTaskCreate(ds18b20_task, "ds18b20", 5120, NULL, 5, NULL) != pdPASS) {
        s_inited = false;
        for (size_t i = 0; i < s_count; i++) {
            ds18b20_del_device(s_dev[i]);
            s_dev[i] = NULL;
        }
        s_count = 0;
        onewire_bus_del(s_bus);
        s_bus = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "%u DS18B20 on GPIO %d (12-bit, every 2s, up to %d on bus)",
        (unsigned)s_count,
        (int)CONFIG_DS18B20_GPIO_NUM,
        DS18B20_APP_MAX_SENSORS);
    return ESP_OK;
}

bool ds18b20_app_get_temperature_c(float *out_c)
{
    if (out_c == NULL) {
        return false;
    }
    if (!s_inited || s_lock == NULL || s_count == 0) {
        return false;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return false;
    }
    bool ok = s_sensors[0].reading_valid;
    *out_c = s_sensors[0].temperature_c;
    xSemaphoreGive(s_lock);
    return ok;
}

size_t ds18b20_app_get_sensors_copy(ds18b20_app_sensor_t *out, size_t max)
{
    if (out == NULL || max == 0) {
        return 0;
    }
    if (!s_inited || s_lock == NULL) {
        return 0;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return 0;
    }
    size_t n = s_count < max ? s_count : max;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_sensors[i];
    }
    xSemaphoreGive(s_lock);
    return n;
}
