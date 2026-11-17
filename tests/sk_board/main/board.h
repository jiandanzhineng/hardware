#pragma once

/* SK control board V1.0, PDF dated 2026-11-17, ESP32-C3FH4. */
#define SK_BAT_GPIO 0
#define SK_HOLD_GPIO 1
#define SK_POWER_KEY_GPIO 2
#define SK_LOCK_GPIO 3
#define SK_LED_GPIO 4
#define SK_KEY1_GPIO 8
#define SK_BOOT_GPIO 9
#define SK_KEY2_GPIO 10
#define SK_BAT_CHANNEL ADC_CHANNEL_0
#define SK_POWER_KEY_CHANNEL ADC_CHANNEL_2

/* User requirement: at most one second energised. Not menuconfig-adjustable. */
#define SK_MAX_PULSE_MS 1000U
#define SK_DEFAULT_PULSE_MS 100U
#define SK_KEY1_PULSE_MS 100U
#define SK_KEY2_PULSE_MS 500U
#define SK_BOOT_PULSE_MS 1000U
#define SK_MIN_PULSE_MS 20U
#define SK_COOLDOWN_US 10000000ULL
#define SK_ARM_WINDOW_US 30000000LL
/* Fire early to leave a margin for normal interrupt latency. */
#define SK_CUTOFF_MARGIN_US 2000ULL
#define SK_BAT_MIN_MV 3300
#define SK_BAT_MAX_MV 4250
