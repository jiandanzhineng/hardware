#include "sdkconfig.h"
#include "board.h"
#include "lock_control.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"

#if CONFIG_SK_LOCK_OUTPUT_VERIFIED
#if !CONFIG_GPTIMER_ISR_IRAM_SAFE || !CONFIG_GPIO_CTRL_FUNC_IN_IRAM || !CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM
#error "Lock cutoff requires GPTimer/GPIO IRAM safety options"
#endif

#if CONFIG_SK_LOCK_ACTIVE_LOW
#define LOCK_ON 0
#define LOCK_OFF 1
#else
#define LOCK_ON 1
#define LOCK_OFF 0
#endif

static DRAM_ATTR gptimer_handle_t timer;
static DRAM_ATTR portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR sk_lock_status_t state;
static DRAM_ATTR uint64_t next_allowed_us;

static bool IRAM_ATTR cutoff(gptimer_handle_t t, const gptimer_alarm_event_data_t *event, void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&mux);
    /* No logging, queueing, malloc, flash access or task scheduling needed. */
    if (gpio_set_level(SK_LOCK_GPIO, LOCK_OFF) != ESP_OK) state.fault = true;
    uint64_t now = event->count_value;
    if (gptimer_get_raw_count(t, &now) != ESP_OK) state.fault = true;
    if (state.active) {
        state.cutoffs++;
        next_allowed_us = now + SK_COOLDOWN_US;
    }
    state.active = false;
    portEXIT_CRITICAL_ISR(&mux);
    return false;
}

esp_err_t sk_lock_init(void)
{
    /* Preload OFF before output-enable to avoid an active pulse. */
    esp_err_t err = gpio_set_level(SK_LOCK_GPIO, LOCK_OFF);
    if (err != ESP_OK) return err;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << SK_LOCK_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if ((err = gpio_config(&io)) != ESP_OK) return err;
    gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, .intr_priority = 3,
    };
    gptimer_event_callbacks_t cb = {.on_alarm = cutoff};
    if ((err = gptimer_new_timer(&cfg, &timer)) != ESP_OK ||
        (err = gptimer_register_event_callbacks(timer, &cb, NULL)) != ESP_OK ||
        (err = gptimer_enable(timer)) != ESP_OK ||
        (err = gptimer_start(timer)) != ESP_OK) {
        state.fault = true;
        return err;
    }
    state.enabled = true;
    /* Also impose a boot delay: a reset cannot immediately re-energise. */
    next_allowed_us = SK_COOLDOWN_US;
    return ESP_OK;
}

esp_err_t sk_lock_pulse(uint32_t ms)
{
    if (ms < SK_MIN_PULSE_MS || ms > SK_MAX_PULSE_MS) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&mux);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    uint64_t now = 0;
    if (!state.enabled || state.fault || state.active) goto done;
    if ((err = gptimer_get_raw_count(timer, &now)) != ESP_OK) goto fault;
    if (now < next_allowed_us) { err = ESP_ERR_INVALID_STATE; goto done; }
    gptimer_alarm_config_t alarm = {
        .alarm_count = now + (uint64_t)ms * 1000 - SK_CUTOFF_MARGIN_US,
        .flags.auto_reload_on_alarm = false,
    };
    /* Arm the running hardware timer BEFORE energising the coil. */
    if ((err = gptimer_set_alarm_action(timer, &alarm)) != ESP_OK) goto fault;
    if ((err = gpio_set_level(SK_LOCK_GPIO, LOCK_ON)) != ESP_OK) goto fault;
    state.active = true;
    state.pulses++;
    goto done;
fault:
    gpio_set_level(SK_LOCK_GPIO, LOCK_OFF);
    state.fault = true; /* No automatic retry after a driver error. */
done:
    portEXIT_CRITICAL(&mux);
    return err;
}

void sk_lock_off(void)
{
    portENTER_CRITICAL(&mux);
    if (gpio_set_level(SK_LOCK_GPIO, LOCK_OFF) != ESP_OK) state.fault = true;
    if (state.active) {
        uint64_t now;
        if (gptimer_get_raw_count(timer, &now) == ESP_OK)
            next_allowed_us = now + SK_COOLDOWN_US;
        else state.fault = true;
    }
    state.active = false;
    /* Keep any pending alarm: a second OFF is harmless and cannot re-enable. */
    portEXIT_CRITICAL(&mux);
}

sk_lock_status_t sk_lock_status(void)
{
    portENTER_CRITICAL(&mux);
    sk_lock_status_t result = state;
    uint64_t now = 0;
    if (state.enabled && gptimer_get_raw_count(timer, &now) == ESP_OK && now < next_allowed_us)
        result.cooldown_ms = (uint32_t)((next_allowed_us - now + 999) / 1000);
    portEXIT_CRITICAL(&mux);
    return result;
}
#else
esp_err_t sk_lock_init(void)
{
    /* This is NOT level shifting: the original hardware still needs fixing. */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << SK_LOCK_GPIO, .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io);
}
esp_err_t sk_lock_pulse(uint32_t ms) { (void)ms; return ESP_ERR_NOT_SUPPORTED; }
void sk_lock_off(void) { }
sk_lock_status_t sk_lock_status(void) { return (sk_lock_status_t){0}; }
#endif
