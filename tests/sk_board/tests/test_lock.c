#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "board.h"
#include "lock_control.h"
#include "test_policy.h"

#if CONFIG_SK_LOCK_ACTIVE_LOW
#define ON 0
#define OFF 1
#else
#define ON 1
#define OFF 0
#endif

static uint64_t clock_us, alarm_us;
static bool running, alarm_failure, read_failure, gpio_failure;
static int output = -1, output_mode = -1;
static unsigned energisations;
static gptimer_event_callbacks_t callbacks;

esp_err_t gpio_set_level(int pin, int level)
{
    assert(pin == SK_LOCK_GPIO);
    if (gpio_failure) return ESP_FAIL;
    if (level == ON) {
        /* A command must never turn on before a cutoff is armed. */
        assert(running && alarm_us > clock_us);
        energisations++;
    }
    output = level;
    return ESP_OK;
}
esp_err_t gpio_config(const gpio_config_t *cfg) { output_mode = cfg->mode; return ESP_OK; }
esp_err_t gptimer_new_timer(const gptimer_config_t *cfg, gptimer_handle_t *out)
{ assert(cfg->resolution_hz == 1000000); *out = (void *)1; return ESP_OK; }
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t t, const gptimer_event_callbacks_t *cb, void *ctx)
{ (void)t; (void)ctx; callbacks = *cb; return ESP_OK; }
esp_err_t gptimer_enable(gptimer_handle_t t) { (void)t; return ESP_OK; }
esp_err_t gptimer_start(gptimer_handle_t t) { (void)t; running = true; return ESP_OK; }
esp_err_t gptimer_get_raw_count(gptimer_handle_t t, uint64_t *value)
{ (void)t; if (read_failure) return ESP_FAIL; *value = clock_us; return ESP_OK; }
esp_err_t gptimer_set_alarm_action(gptimer_handle_t t, const gptimer_alarm_config_t *cfg)
{
    (void)t;
    if (alarm_failure) return ESP_FAIL;
    assert(!cfg->flags.auto_reload_on_alarm);
    alarm_us = cfg->alarm_count;
    return ESP_OK;
}
static void fire_at(uint64_t time)
{
    clock_us = time;
    gptimer_alarm_event_data_t event = {.count_value = time, .alarm_value = alarm_us};
    alarm_us = 0;
    callbacks.on_alarm((void *)1, &event, NULL);
}

static void policy_tests(void)
{
    uint32_t duration = 0;
    const char *bad[] = {"", "0", "19", "1001", "999999999999999999999999", "-1", "+100", "100junk", "1.0", "100 200"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) assert(!sk_parse_duration(bad[i], &duration));
    assert(sk_parse_duration("20", &duration) && duration == 20);
    assert(sk_parse_duration("1000", &duration) && duration == 1000);
    assert(!sk_battery_allows_pulse(false, 3700));
    assert(!sk_battery_allows_pulse(true, -1));
    assert(!sk_battery_allows_pulse(true, 3299));
    assert(sk_battery_allows_pulse(true, 3300));
    assert(sk_battery_allows_pulse(true, 4250));
    assert(!sk_battery_allows_pulse(true, 4251));
    assert(sk_battery_mv(2100) == 4200 && sk_battery_mv(-1) == -1);
    assert(!sk_power_key_pressed(false, 799));
    assert(sk_power_key_pressed(false, 800));
    assert(sk_power_key_pressed(true, 400));
    assert(!sk_power_key_pressed(true, 399));
    sk_debounce_t k = {0};
    assert(!sk_debounce_update(&k, true, 100));
    assert(!sk_debounce_update(&k, false, 10000));
    assert(!sk_debounce_update(&k, true, 20000));
    assert(!sk_debounce_update(&k, true, 49999));
    assert(sk_debounce_update(&k, true, 50000) && k.stable);
    assert(!sk_debounce_update(&k, true, 99999));
}

int main(int argc, char **argv)
{
    policy_tests();
    assert(sk_lock_init() == ESP_OK);
#if !CONFIG_SK_LOCK_OUTPUT_VERIFIED
    (void)argc; (void)argv;
    assert(output_mode == GPIO_MODE_DISABLE);
    assert(!sk_lock_status().enabled);
    assert(sk_lock_pulse(100) == ESP_ERR_NOT_SUPPORTED);
    sk_lock_off();
    assert(output == -1 && energisations == 0);
    puts("PASS default: GPIO high-Z; all pulse requests blocked; parser/ADC/key policy");
    return 0;
#else
    assert(output == OFF);
    assert(sk_lock_pulse(100) == ESP_ERR_INVALID_STATE); /* Boot cooldown. */
    clock_us = SK_COOLDOWN_US;
    const char *scenario = argc > 1 ? argv[1] : "normal";
    if (!strcmp(scenario, "alarm_failure")) {
        alarm_failure = true;
        assert(sk_lock_pulse(100) == ESP_FAIL);
        assert(output == OFF && !energisations && sk_lock_status().fault);
        alarm_failure = false;
        assert(sk_lock_pulse(100) == ESP_ERR_INVALID_STATE);
    } else if (!strcmp(scenario, "read_failure")) {
        read_failure = true;
        assert(sk_lock_pulse(100) == ESP_FAIL);
        assert(output == OFF && !energisations && sk_lock_status().fault);
    } else if (!strcmp(scenario, "gpio_failure")) {
        gpio_failure = true;
        assert(sk_lock_pulse(100) == ESP_FAIL);
        assert(!energisations && sk_lock_status().fault);
    } else {
        assert(sk_lock_pulse(0) == ESP_ERR_INVALID_ARG);
        assert(sk_lock_pulse(1001) == ESP_ERR_INVALID_ARG);
        uint64_t started = clock_us;
        assert(sk_lock_pulse(1000) == ESP_OK && output == ON);
        assert(alarm_us == started + 998000);
        assert(sk_lock_pulse(1000) == ESP_ERR_INVALID_STATE);
        assert(alarm_us == started + 998000); /* Repeats cannot extend ON. */
        /* No application loop runs: the timer callback alone must switch off. */
        fire_at(alarm_us + 1000); /* Simulated 1ms interrupt latency. */
        assert(output == OFF && !sk_lock_status().active && sk_lock_status().cutoffs == 1);
        assert(clock_us - started < 1000000);
        assert(sk_lock_pulse(20) == ESP_ERR_INVALID_STATE);
        clock_us += SK_COOLDOWN_US - 1;
        assert(sk_lock_pulse(20) == ESP_ERR_INVALID_STATE);
        clock_us++;
        assert(sk_lock_pulse(20) == ESP_OK);
        clock_us += 1000;
        sk_lock_off();
        assert(output == OFF && !sk_lock_status().active);
        uint32_t cooldown = sk_lock_status().cooldown_ms;
        sk_lock_off(); /* OFF while idle cannot keep extending the cooldown. */
        assert(sk_lock_status().cooldown_ms == cooldown);
        fire_at(alarm_us); /* Pending alarm after cancellation is harmless. */
        assert(output == OFF && sk_lock_status().cutoffs == 1);
        assert(sk_lock_pulse(100) == ESP_ERR_INVALID_STATE);
        clock_us += SK_COOLDOWN_US;
        assert(sk_lock_pulse(100) == ESP_OK);
        fire_at(alarm_us);
        assert(output == OFF && energisations == 3);
    }
    printf("PASS active_%s: %s\n", CONFIG_SK_LOCK_ACTIVE_LOW ? "low" : "high", scenario);
    return 0;
#endif
}
