#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool enabled;
    bool active;
    bool fault;
    uint32_t pulses;
    uint32_t cutoffs;
    uint32_t cooldown_ms;
} sk_lock_status_t;

/* Call earliest in app_main. Disabled build leaves GPIO3 high impedance. */
esp_err_t sk_lock_init(void);
esp_err_t sk_lock_pulse(uint32_t ms);
void sk_lock_off(void);
sk_lock_status_t sk_lock_status(void);
