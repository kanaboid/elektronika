#pragma once

#include <stdbool.h>

#include "esp_err.h"

/** Inicjalizacja NVS, WiFi STA i SNTP (jesli wlaczone w menuconfig). */
esp_err_t wifi_time_init(void);

bool wifi_time_sta_connected(void);
bool wifi_time_ntp_synced(void);

/** Krotki opis stanu (WiFi / NTP) do konsoli. */
void wifi_time_print_status(void);
