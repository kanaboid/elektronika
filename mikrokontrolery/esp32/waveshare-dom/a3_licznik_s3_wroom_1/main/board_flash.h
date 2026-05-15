#pragma once

#include "esp_err.h"

/** Wykrycie 8 MB flash, log partycji, montaż SPIFFS na /storage. */
esp_err_t board_flash_init(void);
