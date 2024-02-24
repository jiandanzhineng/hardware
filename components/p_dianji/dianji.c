#include "dianji.h"
#include "device_common.h"
#include "single_device_common.h"
#include "esp_log.h"


#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"


#define DIMMABLE_GPIO_0 GPIO_NUM_6
#define DIMMABLE_GPIO_1 GPIO_NUM_7

#define ONOFF_GPIO_0 GPIO_NUM_2
#define ONOFF_GPIO_1 GPIO_NUM_3

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO (GPIO_NUM_7) // Define the output GPIO
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
//#define LEDC_DUTY (491)                // Set duty to 6%. ((2 ** 13) - 1) * 6% = 491
#define LEDC_DUTY (180)                // Set duty to 6%. ((2 ** 13) - 1) * 6% = 491
#define LEDC_FREQUENCY (1000)           // Frequency in Hertz. Set frequency at 5 kHz

#define GPIO_INPUT_TOUCH_SENSER     GPIO_NUM_4
#define GPIO_INPUT_PIN_SEL  (1ULL<<GPIO_INPUT_TOUCH_SENSER) 

#define ESP_INTR_FLAG_DEFAULT 0

static const char *TAG = "dianji";


device_property_t voltage_property;
device_property_t delay_property;
device_property_t shock_property;
extern device_property_t device_type_property;
extern device_property_t sleep_time_property;

device_property_t *device_properties[] = {
    &device_type_property,
    &sleep_time_property,
    &voltage_property,
    &delay_property,
    &shock_property,
};

int device_properties_num = sizeof(device_properties) / sizeof(device_properties[0]);

int adc_raw = 0;
int voltage = 0;
int current_duty = 0;

typedef struct Pid
{
    float Sv; //用户设定值
    float Pv;   //实际值

    float Kp;
    float ki;
    float kd;

    float Ek;   //本次偏差
    float Ek_1; //上次偏差
    float SEk;  //历史偏差之和

    float Iout;
    float Pout;
    float Dout;

    float OUT;
    int times;
} PID;

PID pid;


void on_mqtt_msg_process(char *topic, cJSON *root){

}
void on_set_property(char *property_name, cJSON *property_value, int msg_id){
    ESP_LOGI(TAG, "on_set_property property_name:%s", property_name);
}

void on_device_init(void){

    ESP_LOGI(TAG, "device_init");
    // init power_property, it is a int between 0 and 255
    voltage_property.readable = true;
    voltage_property.writeable = true;
    strcpy(voltage_property.name, "voltage");
    voltage_property.value_type = PROPERTY_TYPE_INT;
    voltage_property.value.int_value = 0;
    voltage_property.max = 500;
    voltage_property.min = 0;

    // init delay_property, it is a int between 100 and 1000
    delay_property.readable = true;
    delay_property.writeable = true;
    strcpy(delay_property.name, "delay");
    delay_property.value_type = PROPERTY_TYPE_INT;
    delay_property.value.int_value = 150;
    delay_property.max = 1000;
    delay_property.min = 100;

    shock_property.readable = true;
    shock_property.writeable = true;
    strcpy(shock_property.name, "shock");
    shock_property.value_type = PROPERTY_TYPE_INT;
    shock_property.value.int_value = 1;
    shock_property.max = 1;
    shock_property.min = 0;

    // pid init
	pid.Kp = 0.1;
	pid.ki= 0.0005;
	pid.kd= 0.1;

    dimmable_plug_pwm_init();

}
void on_device_first_ready(void){
    ESP_LOGI(TAG, "device_first_ready");
    // start heartbeat task every 30 seconds
    xTaskCreate(heartbeat_task, "heartbeat_task", 1024 * 2, NULL, 10, NULL);
    xTaskCreate(task_dianji, "dianji_task", 1024 * 2, NULL, 10, NULL);
    xTaskCreate(Task_adc_boost, "Task_adc_boost", 1024 * 10, NULL, 1, NULL);
}
void on_action(cJSON *root){
    char *method = cJSON_GetObjectItem(root, "method")->valuestring;
    if(strcmp(method, "dian") == 0){
        int time = cJSON_GetObjectItem(root, "time")->valueint;
        int voltage = cJSON_GetObjectItem(root, "voltage")->valueint;
        voltage_property.value.int_value = voltage;
        shock_property.value.int_value = 1;
        ESP_LOGI(TAG, "dian time: %d, voltage: %d", time, voltage);
        vTaskDelay(time / portTICK_PERIOD_MS);
        shock_property.value.int_value = 0;
        control_ledc(LEDC_CHANNEL, (uint32_t)0);
        ESP_LOGI(TAG, "dian end"); 
    }
}

static void control_ledc(ledc_channel_t channel, uint32_t duty)
{
    //ESP_LOGI(TAG, "control_ledc CHANNEL: %d DUTY: %ld", channel, duty);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, channel, duty));
    // Update duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, channel));
}

static void dimmable_plug_pwm_init(void)
{
    ESP_LOGI(TAG, "init pwm");
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY, // Set output frequency at 5 kHz
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LEDC_OUTPUT_IO,
        .duty = 0, // Set duty to 0%
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Set duty
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY));
    // Update duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
}

static bool example_adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Calibration Success");
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated)
    {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    }
    else
    {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}


void PID_Calc() // pid计算
{
	float DelEk;
	float ti, ki;
	float td;
	float kd;
	float out;
	pid.Ek = pid.Sv - pid.Pv;	//得到当前的偏差值
	pid.Pout = pid.Kp * pid.Ek; //比例输出
	pid.SEk += pid.Ek;			//历史偏差总和
	DelEk = pid.Ek - pid.Ek_1;	//最近两次偏差之差
	pid.Iout = pid.ki * pid.SEk; 	//积分输出
	pid.Dout = pid.kd * DelEk; //微分输出

	out = pid.Pout + pid.Iout + pid.Dout;

	if (out > 1000)
	{
		pid.OUT = 1000;
	}
	else if (out <= 0)
	{
		pid.OUT = 0;
	}
	else
	{
		pid.OUT = out;
	}
	pid.Ek_1 = pid.Ek; //更新偏差
}

void Task_adc_boost(void *pvParam)
{
    ESP_LOGI("Task_adc_boost", "Configure ADC");
    //-------------ADC1 Init---------------//
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, USER_ADC1_CHAN, &config));
    //-------------ADC1 Calibration Init---------------//
    adc_cali_handle_t adc1_cali_handle = NULL;
    bool do_calibration1 = example_adc_calibration_init(ADC_UNIT_1, ADC_ATTEN_DB_11, &adc1_cali_handle);
    int voltage_run_time = 0;
    while (1)
    {

        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, USER_ADC1_CHAN, &adc_raw));
        //ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, USER_ADC1_CHAN, adc_raw);
        if (do_calibration1)
        {
            voltage_run_time ++;
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, adc_raw, &voltage));
            voltage = voltage * 21.36;
            int target_voltage = voltage_property.value.int_value * 100;
            if(target_voltage > 50000){
                target_voltage = 50000;
            }
            int duty = current_duty;
            pid.Sv = target_voltage;
            pid.Pv = voltage;
            PID_Calc();
            duty = pid.OUT;
            control_ledc(LEDC_CHANNEL, (uint32_t)duty);

            if(voltage_run_time > 10){
                voltage_run_time = 0;
                ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, USER_ADC1_CHAN, voltage);
            }
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void task_dianji(void *pvParam)
{

    ESP_LOGI("Task_dianji", "start");
    gpio_reset_pin(GPIO_NUM_3);
    // gpio_reset_pin(GPIO_NUM_7);
    gpio_reset_pin(GPIO_NUM_10);
    gpio_reset_pin(GPIO_NUM_2);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(GPIO_NUM_3, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    int a = 0;
    while (1)
    {
        vTaskDelay(delay_property.value.int_value / portTICK_PERIOD_MS);
        if(shock_property.value.int_value == 0){
            continue;
        }

        if (a == 0)
        {
            gpio_set_level(GPIO_NUM_3, 0);
            gpio_set_level(GPIO_NUM_10, 1);
            gpio_set_level(GPIO_NUM_2, 1);
            //ESP_LOGI("Task_Freq_adj", "BOOSTRUN");
            a = 1;
        }
        else if (a == 1)
        {
            gpio_set_level(GPIO_NUM_10, 0);
            gpio_set_level(GPIO_NUM_3, 1);
            gpio_set_level(GPIO_NUM_2, 0);
            //ESP_LOGI("Task_Freq_adj", "BOOSTRUN");
            a = 0;
        }

        //方法1 大于10才会稳定  但是会很宽
        //vTaskDelay(5 / portTICK_PERIOD_MS); 

        //方法2
        //vTaskDelay(10 / portTICK_PERIOD_MS); 
        ESP_LOGI("Task_Freq_adj", "ENDBOOST");


        gpio_set_level(GPIO_NUM_10, 0);
        gpio_set_level(GPIO_NUM_3, 0);
        gpio_set_level(GPIO_NUM_2, 1);

        
    }
}