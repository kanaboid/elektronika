#pragma once

#include "esp_err.h"

/** NeoPixel WS2812 na płytce Waveshare (GPIO38). */
#define STATUS_LED_GPIO  38

typedef enum {
    STATUS_LED_BOOT,        /**< Niebieski — start / inicjalizacja */
    STATUS_LED_READY,       /**< Zielony — PWM gotowe, czeka na demo */
    STATUS_LED_SWEEP_UP,    /**< Zielony — sweep rośnie (0 → max) */
    STATUS_LED_SWEEP_DOWN,  /**< Pomarańczowy — sweep maleje */
    STATUS_LED_IDLE,        /**< Cyan — postój (RPM ≈ 0) */
    STATUS_LED_ERROR,       /**< Czerwony — błąd krytyczny */
    STATUS_LED_OFF,         /**< Wyłączony */
} status_led_state_t;

esp_err_t status_led_init(void);

/** Ustaw kolor według stanu aplikacji (GRB → WS2812). */
void status_led_set(status_led_state_t state);

/** Krótka sekwencja przy starcie (3× niebieski błysk). */
void status_led_boot_sequence(void);
