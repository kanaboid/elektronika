/*
 * Audi A3 8L (Jaeger) — generator sygnałów obrotów i prędkości
 * Płytka: Waveshare ESP32-S3-DEV-KIT-N8R8 (DevKitC-1 pinout)
 *
 * Okablowanie (przez TLP621 / P621):
 *   GPIO4  → 330 Ω → pin 1 opto  → pin 11 niebieski (obroty — często tylko CAN)
 *   GPIO5  → 330 Ω → pin 1 opto  → pin 28 niebieski (prędkość)
 *   +12 V → 4,7 kΩ → pin 28 (pull-up)
 *   GND ESP32 = pin 7 niebieski = GND KORAD
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_flash.h"
#include "status_led.h"

static const char *TAG = "a3_cluster";

#define GPIO_TACH   4
#define GPIO_SPEED  5

#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_SRC_CLK_HZ     80000000U

#define TACH_PPR        2.0f
/* Kalibracja: speed 100 pokazywalo 260 -> 12.0 * (100/260) ≈ 4.6 */
#define SPEED_PPM       3.3195f

#define TASK_STACK      4096
#define TASK_PRIO       5
#define SWEEP_STEP_RPM  200
#define SWEEP_DELAY_MS  80

typedef struct {
    int gpio;
    ledc_timer_t timer;
    ledc_channel_t channel;
    bool stopped;
    bool timer_ready;
} pwm_out_t;

static pwm_out_t s_tach = {
    .gpio = GPIO_TACH,
    .timer = LEDC_TIMER_0,
    .channel = LEDC_CHANNEL_0,
    .stopped = true,
    .timer_ready = false,
};

static pwm_out_t s_speed = {
    .gpio = GPIO_SPEED,
    .timer = LEDC_TIMER_1,
    .channel = LEDC_CHANNEL_1,
    .stopped = true,
    .timer_ready = false,
};

static status_led_state_t s_led_state = STATUS_LED_OFF;

/* Test ułamkowego Hz na GPIO prędkości (esp_timer, obok LEDC) */
static esp_timer_handle_t s_frac_hz_timer;
static bool s_frac_hz_active;
static bool s_frac_hz_level;

static void frac_hz_timer_cb(void *arg)
{
    (void)arg;
    s_frac_hz_level = !s_frac_hz_level;
    gpio_set_level(GPIO_SPEED, s_frac_hz_level);
}

static void speed_frac_hz_stop(void)
{
    if (s_frac_hz_timer != NULL) {
        esp_timer_stop(s_frac_hz_timer);
    }
    s_frac_hz_active = false;
    gpio_set_level(GPIO_SPEED, 0);
}

static esp_err_t speed_frac_hz_start(float hz)
{
    if (hz < 0.1f) {
        speed_frac_hz_stop();
        return ESP_OK;
    }

    if (!s_speed.stopped && s_speed.timer_ready) {
        ledc_stop(LEDC_MODE, s_speed.channel, 0);
        s_speed.stopped = true;
    }
    speed_frac_hz_stop();

    gpio_reset_pin(GPIO_SPEED);
    gpio_set_direction(GPIO_SPEED, GPIO_MODE_OUTPUT);

    double half_us = 1000000.0 / (2.0 * (double)hz);
    uint64_t period_us = (uint64_t)llround(half_us);
    if (period_us < 10) {
        period_us = 10;
    }

    if (s_frac_hz_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = &frac_hz_timer_cb,
            .name = "speed_frac_hz",
        };
        esp_err_t err = esp_timer_create(&args, &s_frac_hz_timer);
        if (err != ESP_OK) {
            return err;
        }
    }

    s_frac_hz_level = false;
    gpio_set_level(GPIO_SPEED, 0);
    esp_err_t err = esp_timer_start_periodic(s_frac_hz_timer, period_us);
    if (err == ESP_OK) {
        s_frac_hz_active = true;
    }
    return err;
}

static esp_err_t pwm_channel_bind_once(pwm_out_t *out)
{
    if (out->timer_ready) {
        return ESP_OK;
    }

    ledc_channel_config_t ch = {
        .gpio_num = out->gpio,
        .speed_mode = LEDC_MODE,
        .channel = out->channel,
        .timer_sel = out->timer,
        .duty = 0,
        .hpoint = 0,
    };
    esp_err_t err = ledc_channel_config(&ch);
    if (err == ESP_OK) {
        out->timer_ready = true;
    }
    return err;
}

static void pwm_set_hz(pwm_out_t *out, uint32_t hz)
{
    if (hz < 1) {
        if (!out->stopped && out->timer_ready) {
            ledc_stop(LEDC_MODE, out->channel, 0);
            out->stopped = true;
        }
        return;
    }

    uint32_t res_bits = ledc_find_suitable_duty_resolution(LEDC_SRC_CLK_HZ, hz);
    if (res_bits == 0) {
        ESP_LOGW(TAG, "LEDC: brak rozdzielczosci dla %lu Hz", (unsigned long)hz);
        return;
    }

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = (ledc_timer_bit_t)res_bits,
        .timer_num = out->timer,
        .freq_hz = hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&timer) != ESP_OK) {
        return;
    }

    if (pwm_channel_bind_once(out) != ESP_OK) {
        return;
    }

    uint32_t duty = (1U << res_bits) / 2U;
    ledc_set_duty(LEDC_MODE, out->channel, duty);
    ledc_update_duty(LEDC_MODE, out->channel);
    out->stopped = false;
}

static uint32_t speed_kmh_to_hz(float kmh)
{
    if (kmh < 0.5f) {
        return 0;
    }
    return (uint32_t)lroundf((kmh / 3.6f) * SPEED_PPM);
}

static uint32_t rpm_to_hz(int rpm)
{
    if (rpm < 1) {
        return 0;
    }
    return (uint32_t)lroundf((rpm / 60.0f) * TACH_PPR);
}

static void cluster_set_rpm(int rpm)
{
    if (rpm < 0) {
        rpm = 0;
    }
    pwm_set_hz(&s_tach, rpm_to_hz(rpm));
}

static void cluster_set_speed_kmh(float kmh)
{
    if (kmh < 0.0f) {
        kmh = 0.0f;
    }
    speed_frac_hz_stop();
    pwm_set_hz(&s_speed, speed_kmh_to_hz(kmh));
}

static void cluster_stop_all(void)
{
    cluster_set_rpm(0);
    speed_frac_hz_stop();
    pwm_set_hz(&s_speed, 0);
    status_led_set(STATUS_LED_IDLE);
}

static void print_cli_help(void)
{
    printf("\n--- Sterowanie licznikiem (wpisz komende + Enter) ---\n");
    printf("  speed <km/h>     np. speed 100   (pin 28, LEDC, cale Hz)\n");
    printf("  hz <Hz>          np. hz 93.5     (pin 28, ułamkowe Hz, test)\n");
    printf("  rpm <obr/min>    np. rpm 3000    (pin 11, moze nie dzialac)\n");
    printf("  stop             wskazowki na 0\n");
    printf("  demo             jednorazowy sweep\n");
    printf("  help             ta pomoc\n");
    printf("Kalibracja: SPEED_PPM=%.3f w main.c (martwa strefa ~40 km/h)\n\n", SPEED_PPM);
}

static void run_demo_sweep(void)
{
    ESP_LOGI(TAG, "Demo sweep 0 -> 6000 obr/min");
    status_led_set(STATUS_LED_SWEEP_UP);

    for (int rpm = 0; rpm <= 6000; rpm += SWEEP_STEP_RPM) {
        cluster_set_rpm(rpm);
        cluster_set_speed_kmh((float)rpm / 35.0f);
        vTaskDelay(pdMS_TO_TICKS(SWEEP_DELAY_MS));
    }

    status_led_set(STATUS_LED_SWEEP_DOWN);
    for (int rpm = 6000; rpm >= 0; rpm -= SWEEP_STEP_RPM) {
        cluster_set_rpm(rpm);
        cluster_set_speed_kmh((float)rpm / 35.0f);
        vTaskDelay(pdMS_TO_TICKS(SWEEP_DELAY_MS));
    }

    status_led_set(STATUS_LED_READY);
    ESP_LOGI(TAG, "Demo zakonczone");
}

static void trim_line(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
}

static int read_line(char *buf, size_t buf_len)
{
    size_t len = 0;
    fputs("\n> ", stdout);
    fflush(stdout);

    while (len + 1 < buf_len) {
        int c = getchar();
        if (c == EOF || c < 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            break;
        }
        if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                fputs("\b \b", stdout);
                fflush(stdout);
            }
            continue;
        }
        buf[len++] = (char)c;
        putchar(c);
        fflush(stdout);
    }
    buf[len] = '\0';
    return (int)len;
}

static void serial_cli_task(void *arg)
{
    (void)arg;
    char line[64];

    vTaskDelay(pdMS_TO_TICKS(500));
    print_cli_help();

    while (true) {
        if (read_line(line, sizeof(line)) <= 0) {
            continue;
        }

        trim_line(line);
        if (line[0] == '\0') {
            continue;
        }

        if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
            print_cli_help();
            continue;
        }

        if (strcmp(line, "stop") == 0) {
            cluster_stop_all();
            printf("OK: stop (0 km/h, 0 obr/min)\n");
            continue;
        }

        if (strcmp(line, "demo") == 0) {
            run_demo_sweep();
            continue;
        }

        if (strncmp(line, "hz ", 3) == 0) {
            float hz = strtof(line + 3, NULL);
            if (hz < 0.1f) {
                speed_frac_hz_stop();
                printf("OK: hz stop (GPIO%d = 0)\n", GPIO_SPEED);
            } else {
                esp_err_t err = speed_frac_hz_start(hz);
                if (err == ESP_OK) {
                    double half_us = 1000000.0 / (2.0 * (double)hz);
                    printf("OK: %.4f Hz na GPIO%d (esp_timer, polokres %.2f us, pin 28)\n",
                           hz, GPIO_SPEED, half_us);
                } else {
                    printf("BLAD hz: %s\n", esp_err_to_name(err));
                }
            }
            continue;
        }

        if (strncmp(line, "speed ", 6) == 0) {
            float kmh = strtof(line + 6, NULL);
            uint32_t hz = speed_kmh_to_hz(kmh);
            cluster_set_speed_kmh(kmh);
            printf("OK: %.0f km/h -> %lu Hz na pin 28\n",
                   kmh, (unsigned long)hz);
            if (kmh > 0.0f && kmh < 40.0f) {
                printf("    (licznik moze pokazac dopiero od ~40 km/h)\n");
            }
            continue;
        }

        if (strncmp(line, "rpm ", 4) == 0) {
            int rpm = (int)strtol(line + 4, NULL, 10);
            uint32_t hz = rpm_to_hz(rpm);
            cluster_set_rpm(rpm);
            printf("OK: %d obr/min -> %lu Hz na pin 11\n",
                   rpm, (unsigned long)hz);
            continue;
        }

        /* skrot: sama liczba = predkosc w km/h */
        char *end = NULL;
        float kmh = strtof(line, &end);
        if (end != line && (*end == '\0' || *end == ' ')) {
            cluster_set_speed_kmh(kmh);
            printf("OK: %.0f km/h -> %lu Hz\n",
                   kmh, (unsigned long)speed_kmh_to_hz(kmh));
            continue;
        }

        printf("Nieznana komenda. Wpisz: help\n");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "A3 8L cluster — tryb komend (idf.py monitor)");
    ESP_LOGI(TAG, "Predkosc: GPIO%d -> pin 28 | Obroty: GPIO%d -> pin 11",
             GPIO_SPEED, GPIO_TACH);

    ESP_ERROR_CHECK(board_flash_init());
    ESP_ERROR_CHECK(status_led_init());
    status_led_boot_sequence();
    status_led_set(STATUS_LED_READY);

    xTaskCreate(serial_cli_task, "cli", TASK_STACK, NULL, TASK_PRIO, NULL);
}
