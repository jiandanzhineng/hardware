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

_Static_assert(SK_KEY1_PULSE_MS >= SK_MIN_PULSE_MS && SK_KEY1_PULSE_MS <= SK_MAX_PULSE_MS, "KEY1 duration");
_Static_assert(SK_KEY2_PULSE_MS >= SK_MIN_PULSE_MS && SK_KEY2_PULSE_MS <= SK_MAX_PULSE_MS, "KEY2 duration");
_Static_assert(SK_BOOT_PULSE_MS >= SK_MIN_PULSE_MS && SK_BOOT_PULSE_MS <= SK_MAX_PULSE_MS, "BOOT duration");

uint32_t sk_button_pulse_update(sk_button_test_t *buttons, uint8_t pressed_mask, int64_t now_us)
{
    if (!buttons->initialized || pressed_mask != buttons->candidate_mask) {
        buttons->initialized = true;
        buttons->candidate_mask = pressed_mask;
        buttons->since_us = now_us;
        return 0;
    }
    if (now_us - buttons->since_us < 30000) return 0;
    if (pressed_mask == 0) {
        buttons->released = true;
        return 0;
    }
    if (!buttons->released) return 0;
    buttons->released = false; /* Holds, chords and rejected attempts consume the press. */
    switch (pressed_mask) {
    case 1: return SK_KEY1_PULSE_MS;
    case 2: return SK_KEY2_PULSE_MS;
    case 4: return SK_BOOT_PULSE_MS;
    default: return 0; /* Multiple keys: require all keys released before another attempt. */
    }
}
