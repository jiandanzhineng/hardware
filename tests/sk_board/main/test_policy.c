#include "test_policy.h"
#include "board.h"

bool sk_parse_duration(const char *text, uint32_t *ms)
{
    if (!text || !*text || !ms) return false;
    uint32_t n = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        n = n * 10U + (unsigned)(*text - '0');
        if (n > SK_MAX_PULSE_MS) return false; /* Reject before overflow. */
    }
    if (n < SK_MIN_PULSE_MS) return false;
    *ms = n;
    return true;
}

bool sk_battery_allows_pulse(bool calibrated, int mv)
{
    return calibrated && mv >= SK_BAT_MIN_MV && mv <= SK_BAT_MAX_MV;
}

int sk_battery_mv(int adc_mv)
{
    /* R4=10k, R7=10k. Do not reuse another board's 3.7x divider. */
    return adc_mv < 0 ? -1 : adc_mv * 2;
}

bool sk_power_key_pressed(bool was_pressed, int mv)
{
    /* R15=R16=10k: pressed ~VBAT/2, released ~0V. Hysteresis. */
    return mv >= (was_pressed ? 400 : 800);
}

bool sk_debounce_update(sk_debounce_t *key, bool pressed, int64_t now_us)
{
    if (pressed != key->candidate) {
        key->candidate = pressed;
        key->since_us = now_us;
    }
    if (key->candidate != key->stable && now_us - key->since_us >= 30000) {
        key->stable = key->candidate;
        return true;
    }
    return false;
}
