#ifndef __BATTERY__H__
#define __BATTERY__H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "driver/gpio.h"

#ifdef CONFIG_DEVICE_TD01
#define BATTERY_ADC_EN GPIO_NUM_1
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0
#endif

#ifdef CONFIG_DEVICE_DIANJI
#define BATTERY_ADC_EN GPIO_NUM_1
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0
#define BATTERY_CLOSE_EN 1
#endif

#ifdef CONFIG_DEVICE_QTZ
#define BATTERY_ADC_EN GPIO_NUM_1
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0
#endif

#ifdef CONFIG_DEVICE_QIYA
#define BATTERY_ADC_EN GPIO_NUM_1
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0
#endif


#ifndef BATTERY_ADC_CHANNEL
// shutdown battery
#define BATTERY_CLOSE_EN 1
#define BATTERY_ADC_EN 0
#define BATTERY_ADC_CHANNEL 0
#endif

#define ARRAY_DIM(a) (sizeof(a) / sizeof((a)[0]))

#define RESISTANCE_VOLTAGE_DIVIDER (3.69)
#define VOLTAGE_COMPENSATION_FACTOR (12)

#define BAT_ADC_MEAS_TIME (10 * 60)

#define BAT_UNDERVOLTAGE_THRESHOLD (25)

esp_err_t battery_adc_get_value(uint8_t *VoltagePer);

#endif /* __BATTERY__H__ */