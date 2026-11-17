#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define IRAM_ATTR
#define DRAM_ATTR
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
#define GPIO_MODE_DISABLE 0
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
typedef struct {
    uint64_t pin_bit_mask;
    int mode, pull_up_en, pull_down_en, intr_type;
} gpio_config_t;
esp_err_t gpio_set_level(int pin, int level);
esp_err_t gpio_config(const gpio_config_t *cfg);
typedef void *gptimer_handle_t;
#define GPTIMER_CLK_SRC_DEFAULT 0
#define GPTIMER_COUNT_UP 0
typedef struct { int clk_src, direction, resolution_hz, intr_priority; } gptimer_config_t;
typedef struct { uint64_t count_value, alarm_value; } gptimer_alarm_event_data_t;
typedef struct {
    bool (*on_alarm)(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *);
} gptimer_event_callbacks_t;
typedef struct {
    uint64_t alarm_count, reload_count;
    struct { unsigned auto_reload_on_alarm : 1; } flags;
} gptimer_alarm_config_t;
esp_err_t gptimer_new_timer(const gptimer_config_t *, gptimer_handle_t *);
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t, const gptimer_event_callbacks_t *, void *);
esp_err_t gptimer_enable(gptimer_handle_t);
esp_err_t gptimer_start(gptimer_handle_t);
esp_err_t gptimer_get_raw_count(gptimer_handle_t, uint64_t *);
esp_err_t gptimer_set_alarm_action(gptimer_handle_t, const gptimer_alarm_config_t *);
