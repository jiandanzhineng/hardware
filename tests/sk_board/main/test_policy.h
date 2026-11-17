#pragma once
#include <stdbool.h>
#include <stdint.h>

bool sk_parse_duration(const char *text, uint32_t *ms);
bool sk_battery_allows_pulse(bool calibrated, int millivolts);
int sk_battery_mv(int adc_mv);
bool sk_power_key_pressed(bool was_pressed, int mv);

typedef struct {
    bool stable;
    bool candidate;
    int64_t since_us;
} sk_debounce_t;
bool sk_debounce_update(sk_debounce_t *key, bool pressed, int64_t now_us);
