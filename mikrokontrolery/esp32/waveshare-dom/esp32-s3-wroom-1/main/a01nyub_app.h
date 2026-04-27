#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool initialized;
    bool reading_valid;
    bool reading_stale;
    bool mode_pin_configured;
    bool mode_realtime;
    uint16_t distance_mm;
    uint64_t last_update_ms;
} a01nyub_status_t;

esp_err_t a01nyub_app_start(void);
bool a01nyub_app_get_distance_mm(uint16_t *out_mm);
a01nyub_status_t a01nyub_app_get_status(void);
esp_err_t a01nyub_app_set_mode_realtime(bool realtime);
bool a01nyub_app_get_mode_realtime(bool *out_realtime);
