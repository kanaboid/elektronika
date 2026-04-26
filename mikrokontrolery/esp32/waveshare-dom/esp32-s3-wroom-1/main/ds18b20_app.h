#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DS18B20_APP_MAX_SENSORS 8

typedef struct {
    uint64_t address;
    char address_hex[17];
    float temperature_c;
    bool reading_valid;
} ds18b20_app_sensor_t;

esp_err_t ds18b20_app_start(void);

bool ds18b20_app_get_temperature_c(float *out_c);
size_t ds18b20_app_get_sensors_copy(ds18b20_app_sensor_t *out, size_t max);
