#include "cJSON.h"
#include "driver/ledc.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#define USER_ADC1_CHAN ADC_CHANNEL_4

extern int device_properties_num;

static void button_single_click_cb(void *arg,void *usr_data);
static void report_all_properties(void);
static void heartbeat_task(void);
static void dimmable_plug_pwm_init(void);
static void control_ledc(ledc_channel_t channel, uint32_t duty);

void task_dianji(void *pvParam);
static bool example_adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle);
void Task_adc_boost(void *pvParam);
