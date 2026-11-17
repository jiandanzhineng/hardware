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

typedef struct {
    uint8_t candidate_mask;
    bool initialized;
    bool released;
    int64_t since_us;
} sk_button_test_t;

/* Bits 0/1/2 = KEY1/KEY2/BOOT pressed. Returns one pulse duration or zero.
   A rejected pulse is consumed: it must never be queued for a later retry. */
uint32_t sk_button_pulse_update(sk_button_test_t *buttons, uint8_t pressed_mask, int64_t now_us);
