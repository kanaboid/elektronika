/*
 * Audi A3 8L (Jaeger) — generator sygnałów obrotów i prędkości
 * Płytka: Waveshare ESP32-S3-DEV-KIT-N8R8 (DevKitC-1 pinout)
 *
 * Okablowanie (przez TLP621 / P621):
 *   GPIO4  → 330 Ω → opto → pin 30 ZIELONEGO T32 (58DE) — masa = jasnosc MAX
 *   GPIO5  → 330 Ω → opto → pin 28 niebieski (predkosc)
 *   +12 V → 4,7 kΩ → pin 28 (pull-up)
 *   Niebieski pin 15 = 58d WYJSCIE cyfrowe licznika (nie podlaczac opto)
 *   Niebieski pin 20 = 58s WYJSCIE analogowe licznika
 *   GND ESP32 = pin 7 niebieski = GND KORAD
 *   Obroty (pin 11): tylko CAN
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_flash.h"
#include "status_led.h"
#include "wifi_time.h"

static const char *TAG = "a3_cluster";

#define GPIO_BACKLIGHT_58DE  4   /* -> zielony T32 pin 30, Klemmen 58DE */
#define GPIO_SPEED           5

#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_SRC_CLK_HZ     80000000U

#define BACKLIGHT_DIM_DEFAULT      85
#define BACKLIGHT_PWM_HZ           100U  /* PWM dla filtru RC (4.7kΩ + 10µF, τ~40ms) */
#define LEDC_DIM_TIMER             LEDC_TIMER_2
#define LEDC_DIM_CHANNEL           LEDC_CHANNEL_2
/* Pin 30 to wejscie analogowe ADC w liczniku (pull-up ~3-4 kΩ do ~5 V).
 * Wymagane: kondensator 10 µF pin 30 -> GND (filtr RC z pull-up licznika).
 * Bez kondensatora dim 1-99%% bedzie skakal max/min (potwierdzone testami). */

#define TASK_STACK      4096

/* Kalibracja reczna: km/h -> Hz (licznik A3 8L, pin 28) */
typedef struct {
    float kmh;
    float hz;
} speed_cal_point_t;

static const speed_cal_point_t s_speed_cal[] = {
    { 5.0f, 4.5f },
    { 10.0f, 8.5f },
    { 15.0f, 13.0f },
    { 20.0f, 18.0f },
    { 25.0f, 22.7f },
    { 30.0f, 32.5f },
    { 50.0f, 47.0f },
    { 70.0f, 66.5f },
    { 80.0f, 76.25f },
    { 90.0f, 87.0f },
    { 100.0f, 96.0f },
    { 120.0f, 114.5f },
    { 140.0f, 132.5f },
    { 150.0f, 141.5f },
    { 160.0f, 150.5f },
    { 180.0f, 168.5f },
    { 200.0f, 186.5f },
    { 250.0f, 231.5f },
};

#define SPEED_CAL_N   (sizeof(s_speed_cal) / sizeof(s_speed_cal[0]))
#define TASK_PRIO       5

#define DEMO_KMH_MAX    250.0f
#define DEMO_KMH_STEP   5.0f
#define DEMO_STEP_MS    100
#define DEMO_HOLD_MS    800

#define CLOCK_TASK_STACK      3072
#define CLOCK_TASK_PRIO       4
#define CLOCK_KMH_PER_HOUR    10.0f   /* 10 km/h = 1 h; w godzinie: +min/60 (17:59 -> ~180) */
#define CLOCK_KMH_PER_MINUTE  1.0f    /* minuta M -> M km/h (0..59) */
#define CLOCK_MINUTE_SHOW_SEC 5       /* przy zmianie minuty: 5 s minuty, potem godzina */

typedef struct {
    int gpio;
    ledc_timer_t timer;
    ledc_channel_t channel;
    bool stopped;
    bool timer_ready;
} pwm_out_t;

static pwm_out_t s_speed = {
    .gpio = GPIO_SPEED,
    .timer = LEDC_TIMER_1,
    .channel = LEDC_CHANNEL_1,
    .stopped = true,
    .timer_ready = false,
};

static status_led_state_t s_led_state = STATUS_LED_OFF;

static volatile bool s_clock_run;
static TaskHandle_t s_clock_task;

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

static esp_err_t speed_frac_hz_start(float hz);

static uint64_t speed_frac_hz_period_us(float hz)
{
    double half_us = 1000000.0 / (2.0 * (double)hz);
    uint64_t period_us = (uint64_t)llround(half_us);
    if (period_us < 10) {
        period_us = 10;
    }
    return period_us;
}

/* Zmiana Hz bez gpio_reset — do demo / szybkiego sweepu */
static esp_err_t speed_frac_hz_set(float hz)
{
    if (hz < 0.1f) {
        speed_frac_hz_stop();
        return ESP_OK;
    }

    uint64_t period_us = speed_frac_hz_period_us(hz);

    if (s_frac_hz_timer != NULL && s_frac_hz_active) {
        esp_err_t err = esp_timer_stop(s_frac_hz_timer);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        err = esp_timer_start_periodic(s_frac_hz_timer, period_us);
        return err;
    }

    return speed_frac_hz_start(hz);
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

    uint64_t period_us = speed_frac_hz_period_us(hz);

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

static int s_backlight_percent = BACKLIGHT_DIM_DEFAULT;
static uint32_t s_dim_max_duty;
static bool s_dim_ledc_ready;

/**
 * Mapowanie user-dim (0..100) -> duty PWM (0..100).
 * Licznik ma PRÓG detekcji ~3 V na pin 30:
 *   V > 3 V  -> backlight OFF (cluster ignoruje)
 *   V < 3 V  -> aktywny, jasnosc rosnie z malejacym V
 * Z C=10 uF dostepny zakres to duty 0 (OFF) lub 9..100 (aktywne).
 * Duty 1..8 daje V w okolicach progu -> migotanie, omijane.
 * Skutek: dim 0 = OFF, dim 1..100 = aktywny zakres ~60..100%% rzeczywistej jasnosci.
 * (Dla pelnego 0..100%% jasnosci potrzebny rezystor szeregowy 2,2-4,7 kΩ w opto.)
 */
typedef struct {
    int user;
    int duty;
} dim_lut_t;

static const dim_lut_t s_dim_lut[] = {
    {   0,   0 },   /* V=4.8 — OFF (poza progiem) */
    {   1,   9 },   /* V=1.2 — pierwsze swiatlo (~60%% jasn.) */
    {  25,  10 },   /* V=1.04 (~65%%) */
    {  50,  25 },   /* V=0.52 (~83%%) */
    {  75,  50 },   /* V=0.30 (~90%%) */
    { 100, 100 },   /* V=0.19 — MAX */
};

#define DIM_LUT_N  (sizeof(s_dim_lut) / sizeof(s_dim_lut[0]))
#define DIM_FLICKER_LO 4
#define DIM_FLICKER_HI 8

static int dim_user_to_duty(int user_pct)
{
    if (user_pct <= 0) {
        return 0;
    }
    if (user_pct >= 100) {
        return 100;
    }

    int duty = 0;
    for (size_t i = 0; i + 1 < DIM_LUT_N; i++) {
        if (user_pct <= s_dim_lut[i + 1].user) {
            int u0 = s_dim_lut[i].user;
            int u1 = s_dim_lut[i + 1].user;
            int d0 = s_dim_lut[i].duty;
            int d1 = s_dim_lut[i + 1].duty;
            int span = u1 - u0;
            duty = (span > 0)
                ? d0 + ((d1 - d0) * (user_pct - u0)) / span
                : d0;
            break;
        }
    }
    /* Bezpiecznik: omin pasmo migotania */
    if (duty >= DIM_FLICKER_LO && duty <= DIM_FLICKER_HI) {
        duty = DIM_FLICKER_HI + 1;
    }
    return duty;
}

static esp_err_t backlight_dim_init(void)
{
    if (s_dim_ledc_ready) {
        return ESP_OK;
    }

    uint32_t res_bits = ledc_find_suitable_duty_resolution(LEDC_SRC_CLK_HZ, BACKLIGHT_PWM_HZ);
    if (res_bits == 0) {
        ESP_LOGE(TAG, "LEDC dim: brak rozdzielczosci dla %u Hz", (unsigned)BACKLIGHT_PWM_HZ);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = (ledc_timer_bit_t)res_bits,
        .timer_num = LEDC_DIM_TIMER,
        .freq_hz = BACKLIGHT_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }

    ledc_channel_config_t ch = {
        .gpio_num = GPIO_BACKLIGHT_58DE,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_DIM_CHANNEL,
        .timer_sel = LEDC_DIM_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) {
        return err;
    }

    s_dim_max_duty = (1U << res_bits) - 1U;
    s_dim_ledc_ready = true;
    ESP_LOGI(TAG, "58DE PWM %u Hz, GPIO%d -> zielony pin 30 (potrzebny C 10uF do GND)",
             (unsigned)BACKLIGHT_PWM_HZ, GPIO_BACKLIGHT_58DE);
    return ESP_OK;
}

/**
 * Jasnost tarczy: zielony pin 30 (58DE) — wejscie analogowe ADC.
 * Wymagany filtr RC: kondensator 10 µF pin 30 -> GND (pull-up ~3-4 kΩ w liczniku
 * daje τ ~40 ms; PWM 100 Hz wygladzony).
 * 0 = MIN, 100 = MAX. Wiecej %% = wiecej GND = jasniej.
 */
static void backlight_dim_set_percent(int pct)
{
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    s_backlight_percent = pct;

    if (!s_dim_ledc_ready) {
        if (backlight_dim_init() != ESP_OK) {
            return;
        }
    }

    int duty_pct = dim_user_to_duty(pct);
    uint32_t duty = ((uint32_t)duty_pct * s_dim_max_duty) / 100U;
    ledc_set_duty(LEDC_MODE, LEDC_DIM_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_DIM_CHANNEL);
}

static void backlight_dim_print_info(void)
{
    int duty_pct = dim_user_to_duty(s_backlight_percent);
    uint32_t duty = 0;
    if (s_dim_ledc_ready && s_dim_max_duty > 0) {
        duty = ((uint32_t)duty_pct * s_dim_max_duty) / 100U;
    }
    printf("dim user %d %% -> duty %d %% | LEDC %u Hz | raw %lu/%lu | GPIO%d -> pin 30\n",
           s_backlight_percent, duty_pct, (unsigned)BACKLIGHT_PWM_HZ,
           (unsigned long)duty, (unsigned long)s_dim_max_duty, GPIO_BACKLIGHT_58DE);
    printf("  Cluster: prog detekcji ~3 V; dim 0 = OFF; dim 1..100 = ~60..100%% jasn.\n");
    printf("  Dla pelnego 0..100%% jasn. dodaj rezystor 2,2-4,7 kΩ szer. w opto\n");
}

static void backlight_dim_slow_stop(void) { /* zostawione dla cluster_stop_all() */ }

static void backlight_dim_gpio_hold(int level)
{
    if (s_dim_ledc_ready) {
        ledc_stop(LEDC_MODE, LEDC_DIM_CHANNEL, level);
        s_dim_ledc_ready = false;
    }
    gpio_reset_pin(GPIO_BACKLIGHT_58DE);
    gpio_set_direction(GPIO_BACKLIGHT_58DE, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_BACKLIGHT_58DE, level);
}

static void backlight_dim_sweep_test(void)
{
    printf("dim sweep: 0,25,50,75,100 co 3 s (PWM %u Hz, wymaga C 10uF na pin 30)\n",
           (unsigned)BACKLIGHT_PWM_HZ);
    const int steps[] = { 0, 25, 50, 75, 100 };
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        backlight_dim_set_percent(steps[i]);
        printf("  dim %d %%\n", steps[i]);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    backlight_dim_set_percent(BACKLIGHT_DIM_DEFAULT);
    printf("dim sweep koniec -> %d %%\n", s_backlight_percent);
}

static float speed_cal_kmh_to_hz(float kmh)
{
    if (kmh < 0.5f) {
        return 0.0f;
    }

    if (kmh < s_speed_cal[0].kmh) {
        return kmh * (s_speed_cal[0].hz / s_speed_cal[0].kmh);
    }

    if (kmh >= s_speed_cal[SPEED_CAL_N - 1].kmh) {
        const speed_cal_point_t *a = &s_speed_cal[SPEED_CAL_N - 2];
        const speed_cal_point_t *b = &s_speed_cal[SPEED_CAL_N - 1];
        float t = (kmh - a->kmh) / (b->kmh - a->kmh);
        return a->hz + t * (b->hz - a->hz);
    }

    for (size_t i = 0; i + 1 < SPEED_CAL_N; i++) {
        const speed_cal_point_t *a = &s_speed_cal[i];
        const speed_cal_point_t *b = &s_speed_cal[i + 1];
        if (kmh <= b->kmh) {
            float t = (kmh - a->kmh) / (b->kmh - a->kmh);
            return a->hz + t * (b->hz - a->hz);
        }
    }

    return 0.0f;
}

static void cluster_set_rpm(int rpm)
{
    (void)rpm;
    /* Obroty tylko CAN; GPIO4 = zielony pin 30 (58DE dim) */
}

static esp_err_t cluster_set_speed_kmh(float kmh)
{
    if (kmh < 0.0f) {
        kmh = 0.0f;
    }

    float hz = speed_cal_kmh_to_hz(kmh);
    if (hz < 0.1f) {
        speed_frac_hz_stop();
        pwm_set_hz(&s_speed, 0);
        return ESP_OK;
    }
    if (s_frac_hz_active) {
        return speed_frac_hz_set(hz);
    }
    return speed_frac_hz_start(hz);
}

static void clock_stop(void);

static void cluster_stop_all(void)
{
    clock_stop();
    backlight_dim_slow_stop();
    speed_frac_hz_stop();
    pwm_set_hz(&s_speed, 0);
    status_led_set(STATUS_LED_IDLE);
}

static bool system_time_is_set(void)
{
    if (wifi_time_ntp_synced()) {
        return true;
    }
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    return (t.tm_year + 1900) >= 2024;
}

static esp_err_t system_time_set_hms(int hour, int min, int sec)
{
    if (hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        return ESP_ERR_INVALID_ARG;
    }

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (!system_time_is_set()) {
        t.tm_year = 126; /* 2026 */
        t.tm_mon = 4;    /* maj */
        t.tm_mday = 15;
        t.tm_isdst = -1;
    }
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    t.tm_isdst = -1;

    time_t epoch = mktime(&t);
    if (epoch == (time_t)-1) {
        return ESP_ERR_INVALID_ARG;
    }

    struct timeval tv = {
        .tv_sec = epoch,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static float clock_time_to_kmh(const struct tm *t, bool *show_minute_out)
{
    bool show_minute = (t->tm_sec < CLOCK_MINUTE_SHOW_SEC);
    if (show_minute_out != NULL) {
        *show_minute_out = show_minute;
    }
    if (show_minute) {
        return (float)t->tm_min * CLOCK_KMH_PER_MINUTE;
    }
    /* Godzina plynnie w obrebie 60 min: 17:00=170, 17:30=175, 17:59~180 km/h */
    float hour_frac = (float)t->tm_hour + (float)t->tm_min / 60.0f;
    return hour_frac * CLOCK_KMH_PER_HOUR;
}

static void clock_task(void *arg)
{
    (void)arg;
    float last_kmh = -1.0f;

    while (s_clock_run) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);

        bool show_minute = false;
        float kmh = clock_time_to_kmh(&t, &show_minute);

        if (kmh != last_kmh) {
            float hz = speed_cal_kmh_to_hz(kmh);
            esp_err_t err = cluster_set_speed_kmh(kmh);
            if (err == ESP_OK) {
                if (show_minute) {
                    printf("clock %02d:%02d:%02d  MIN=%02d -> %.0f km/h (%.2f Hz)\n",
                           t.tm_hour, t.tm_min, t.tm_sec, t.tm_min, kmh, hz);
                } else {
                    float hour_frac = (float)t.tm_hour + (float)t.tm_min / 60.0f;
                    printf("clock %02d:%02d:%02d  GODZ=%.2f -> %.1f km/h (%.2f Hz)\n",
                           t.tm_hour, t.tm_min, t.tm_sec, hour_frac, kmh, hz);
                }
            } else {
                printf("clock BLAD: %s\n", esp_err_to_name(err));
            }
            fflush(stdout);
            last_kmh = kmh;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    s_clock_task = NULL;
    vTaskDelete(NULL);
}

static void clock_stop(void)
{
    if (!s_clock_run && s_clock_task == NULL) {
        return;
    }
    s_clock_run = false;
    for (int i = 0; i < 30 && s_clock_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void clock_start(void)
{
    if (s_clock_task != NULL) {
        printf("clock juz dziala (wpisz: stop)\n");
        return;
    }

    if (!system_time_is_set()) {
        printf("Uwaga: brak czasu — poczekaj na NTP (wifi) lub: time HH:MM:SS\n");
    }

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    bool show_minute = false;
    float kmh = clock_time_to_kmh(&t, &show_minute);
    cluster_set_speed_kmh(kmh);
    printf("clock START: %02d:%02d:%02d -> %.0f km/h (%s)\n",
           t.tm_hour, t.tm_min, t.tm_sec, kmh,
           show_minute ? "minuty 5s po :00" : "godzina (10 km/h = 1h)");
    printf("  godzina: (h+min/60)*10 km/h | minuta: %d min -> %.0f km/h (pierwsze %d s)\n",
           t.tm_min, (float)t.tm_min * CLOCK_KMH_PER_MINUTE, CLOCK_MINUTE_SHOW_SEC);

    s_clock_run = true;
    if (xTaskCreate(clock_task, "clock", CLOCK_TASK_STACK, NULL, CLOCK_TASK_PRIO,
                    &s_clock_task) != pdPASS) {
        s_clock_run = false;
        s_clock_task = NULL;
        printf("BLAD: nie mozna utworzyc zadania clock\n");
    }
}

static bool parse_hms(const char *text, int *hour, int *min, int *sec)
{
    int h = 0;
    int m = 0;
    int s = 0;
    char sep1 = 0;
    char sep2 = 0;

    if (sscanf(text, "%d%c%d%c%d", &h, &sep1, &m, &sep2, &s) != 5) {
        return false;
    }
    if ((sep1 != ':' && sep1 != '.') || (sep2 != ':' && sep2 != '.')) {
        return false;
    }
    *hour = h;
    *min = m;
    *sec = s;
    return true;
}

static void print_cli_help(void)
{
    printf("\n--- Sterowanie licznikiem (wpisz komende + Enter) ---\n");
    printf("  speed <km/h>     np. speed 100   (pin 28, tabela kalibracji)\n");
    printf("  hz <Hz>          np. hz 93.5     (pin 28, reczne Hz, test)\n");
    printf("  dim <0-100>     jasnosc (pin 30 / 58DE, PWM %u Hz, wymaga C 10uF)\n",
           (unsigned)BACKLIGHT_PWM_HZ);
    printf("  dim              stan PWM\n");
    printf("  dim test on|off  on=MAX (GND), off=MIN (puszczenie)\n");
    printf("  dim sweep        0..100 co 3 s\n");
    printf("  rpm <obr/min>    wylaczone (obroty CAN; GPIO4 = dim)\n");
    printf("  stop             wskazowki na 0\n");
    printf("  demo             sweep predkosci 0..250 km/h\n");
    printf("  time HH:MM:SS    recznie ustaw zegar\n");
    printf("  clock            zegar: godzina plynnie (17:59~180), min 5s po :00\n");
    printf("  wifi             stan WiFi / NTP\n");
    printf("  help             ta pomoc\n");
    printf("  WiFi/NTP: idf.py menuconfig -> A3 Cluster — siec i czas\n");
    printf("Kalibracja: tabela %u punktow (5..250 km/h), interpolacja liniowa\n", (unsigned)SPEED_CAL_N);
    printf("  wskazowka moze stac do ~40 km/h mimo sygnalu\n\n");
}

static void run_demo_sweep(void)
{
    clock_stop();

    printf("Demo: 0 -> %.0f km/h (krok %.0f), potem powrot...\n",
           DEMO_KMH_MAX, DEMO_KMH_STEP);
    fflush(stdout);

    status_led_set(STATUS_LED_SWEEP_UP);

    for (float kmh = 0.0f; kmh <= DEMO_KMH_MAX + 0.01f; kmh += DEMO_KMH_STEP) {
        esp_err_t err = cluster_set_speed_kmh(kmh);
        if (err != ESP_OK) {
            printf("BLAD demo @ %.0f km/h: %s\n", kmh, esp_err_to_name(err));
            break;
        }
        if ((int)kmh % 50 == 0) {
            printf("  %.0f km/h -> %.2f Hz\n", kmh, speed_cal_kmh_to_hz(kmh));
            fflush(stdout);
        }
        vTaskDelay(pdMS_TO_TICKS(DEMO_STEP_MS));
    }

    vTaskDelay(pdMS_TO_TICKS(DEMO_HOLD_MS));

    status_led_set(STATUS_LED_SWEEP_DOWN);
    for (float kmh = DEMO_KMH_MAX; kmh >= 0.0f; kmh -= DEMO_KMH_STEP) {
        esp_err_t err = cluster_set_speed_kmh(kmh);
        if (err != ESP_OK) {
            printf("BLAD demo @ %.0f km/h: %s\n", kmh, esp_err_to_name(err));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(DEMO_STEP_MS));
    }

    cluster_stop_all();
    status_led_set(STATUS_LED_READY);
    printf("Demo zakonczone.\n");
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
            printf("OK: stop (0 km/h; dim bez zmian)\n");
            continue;
        }

        if (strcmp(line, "demo") == 0) {
            run_demo_sweep();
            fputs("\n> ", stdout);
            fflush(stdout);
            continue;
        }

        if (strncmp(line, "time ", 5) == 0) {
            int hour = 0;
            int min = 0;
            int sec = 0;
            if (!parse_hms(line + 5, &hour, &min, &sec)) {
                printf("Uzycie: time HH:MM:SS  (np. time 14:30:00)\n");
                continue;
            }
            esp_err_t err = system_time_set_hms(hour, min, sec);
            if (err == ESP_OK) {
                time_t now = time(NULL);
                struct tm t;
                localtime_r(&now, &t);
                printf("OK: zegar %02d:%02d:%02d (%04d-%02d-%02d)\n",
                       t.tm_hour, t.tm_min, t.tm_sec,
                       t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
            } else {
                printf("BLAD time: %s\n", esp_err_to_name(err));
            }
            continue;
        }

        if (strcmp(line, "clock") == 0) {
            clock_start();
            continue;
        }

        if (strcmp(line, "wifi") == 0) {
            wifi_time_print_status();
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
            float hz = speed_cal_kmh_to_hz(kmh);
            esp_err_t err = cluster_set_speed_kmh(kmh);
            if (err == ESP_OK) {
                printf("OK: %.1f km/h -> %.2f Hz na pin 28\n", kmh, hz);
                if (kmh > 0.0f && kmh < 40.0f) {
                    printf("    (wskazowka moze stac do ~40 km/h)\n");
                }
            } else {
                printf("BLAD speed: %s\n", esp_err_to_name(err));
            }
            continue;
        }

        if (strcmp(line, "dim") == 0) {
            backlight_dim_print_info();
            continue;
        }

        if (strcmp(line, "dim sweep") == 0) {
            backlight_dim_sweep_test();
            continue;
        }

        if (strcmp(line, "dim test on") == 0) {
            backlight_dim_gpio_hold(1);
            printf("OK: GPIO%d=1 -> pin 30 zielony na GND = jasnosc MAX\n", GPIO_BACKLIGHT_58DE);
            continue;
        }

        if (strcmp(line, "dim test off") == 0) {
            backlight_dim_gpio_hold(0);
            printf("OK: GPIO%d=0 -> pin 30 puszczony = jasnosc MIN\n", GPIO_BACKLIGHT_58DE);
            continue;
        }

        if (strncmp(line, "dim ", 4) == 0) {
            char *arg = line + 4;
            while (*arg == ' ') {
                arg++;
            }
            if (strncmp(arg, "test ", 5) == 0) {
                printf("Uzycie: dim test on | dim test off\n");
                continue;
            }
            int pct = (int)strtol(arg, NULL, 10);
            backlight_dim_set_percent(pct);
            int duty_pct = dim_user_to_duty(s_backlight_percent);
            printf("OK: dim %d %% -> duty %d %% (PWM %u Hz, GPIO%d -> pin 30 + C 10uF)\n",
                   s_backlight_percent, duty_pct, (unsigned)BACKLIGHT_PWM_HZ, GPIO_BACKLIGHT_58DE);
            continue;
        }

        if (strncmp(line, "rpm ", 4) == 0) {
            (void)strtol(line + 4, NULL, 10);
            printf("rpm: wylaczone — obroty CAN; GPIO%d = dim (zielony pin 30). Uzyj: dim <0-100>\n",
                   GPIO_BACKLIGHT_58DE);
            continue;
        }

        /* skrot: sama liczba = predkosc w km/h */
        char *end = NULL;
        float kmh = strtof(line, &end);
        if (end != line && (*end == '\0' || *end == ' ')) {
            float hz = speed_cal_kmh_to_hz(kmh);
            esp_err_t err = cluster_set_speed_kmh(kmh);
            if (err == ESP_OK) {
                printf("OK: %.1f km/h -> %.2f Hz\n", kmh, hz);
            } else {
                printf("BLAD: %s\n", esp_err_to_name(err));
            }
            continue;
        }

        printf("Nieznana komenda. Wpisz: help\n");
    }
}

static void timezone_init(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

void app_main(void)
{
    ESP_LOGI(TAG, "A3 8L cluster — tryb komend (idf.py monitor)");
    ESP_LOGI(TAG, "Predkosc: GPIO%d -> pin 28 | dim 58DE: GPIO%d -> zielony pin 30",
             GPIO_SPEED, GPIO_BACKLIGHT_58DE);

    timezone_init();

    esp_err_t werr = wifi_time_init();
    if (werr == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "WiFi: ustaw SSID w menuconfig");
    } else if (werr != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init: %s", esp_err_to_name(werr));
    }

    ESP_ERROR_CHECK(board_flash_init());
    ESP_ERROR_CHECK(status_led_init());
    if (backlight_dim_init() == ESP_OK) {
        backlight_dim_set_percent(BACKLIGHT_DIM_DEFAULT);
    }
    status_led_boot_sequence();
    status_led_set(STATUS_LED_READY);

    xTaskCreate(serial_cli_task, "cli", TASK_STACK, NULL, TASK_PRIO, NULL);
}
