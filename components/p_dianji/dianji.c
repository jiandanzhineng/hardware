#include "dianji.h"
#include "device_common.h"
#include "single_device_common.h"
#include "esp_log.h"


#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"


#define OUTPUT_TIME 100



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




void on_mqtt_msg_process(char *topic, cJSON *root){

}
void on_set_property(char *property_name, cJSON *property_value, int msg_id){
    ESP_LOGI(TAG, "on_set_property property_name:%s", property_name);
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
        // control_ledc(LEDC_CHANNEL, (uint32_t)0);
        ESP_LOGI(TAG, "dian end"); 
    }
}

typedef struct
{
    float kp, ki, kd;            // 三个系数
    float error, lastError;      // 误差、上次误差
    float integral, maxIntegral; // 积分、积分限幅
    float output, maxOutput;     // 输出、输出限幅
} PID;

// 用于初始化pid参数的函数
void PID_Init(PID *pid, float p, float i, float d, float maxI, float maxOut)
{
    pid->kp = p;
    pid->ki = i;
    pid->kd = d;
    pid->maxIntegral = maxI;
    pid->maxOutput = maxOut;
}

// 进行一次pid计算
// 参数为(pid结构体,目标值,反馈值)，计算结果放在pid结构体的output成员中
void PID_Calc(PID *pid, float reference, float feedback)
{
    // 更新数据
    pid->lastError = pid->error;       // 将旧error存起来
    pid->error = reference - feedback; // 计算新error
    // 计算微分
    float dout = (pid->error - pid->lastError) * pid->kd;
    // 计算比例
    float pout = pid->error * pid->kp;
    // 计算积分
    pid->integral += pid->error * pid->ki;
    // 积分限幅
    if (pid->integral > pid->maxIntegral)
        pid->integral = pid->maxIntegral;
    else if (pid->integral < -pid->maxIntegral)
        pid->integral = -pid->maxIntegral;
    // 计算输出
    pid->output = pout + dout + pid->integral;
    // 输出限幅
    if (pid->output > pid->maxOutput)
        pid->output = pid->maxOutput;
    else if (pid->output < -pid->maxOutput)
        pid->output = -pid->maxOutput;
}

// 创建一个PID结构体变量
PID mypid = {0};

//  PID 三个系数
float kp = 3.5;
float ki = 0.002;
float kd = 20;

// 积分限幅
float maxIntegral = 50;
// 输出限幅
float maxOutput = 140;

// 通过两个输出引脚控制
#define O1 3
#define O2 13

// PWM boost升压控制引脚
#define PWM 10

#define ADC_GPIO 4

// PWM初始频率
const int F_B = 100;

// 每5s改变一次
const int CHANGE_TIME = 5;

#define V_CHANGE_01 20
#define V_CHANGE_02 50
#define V_CHANGE_03 90
#define V_CHANGE_04 100

// 每次的电压变化
const int V_CHANGE[6] = {V_CHANGE_01, V_CHANGE_02, V_CHANGE_03, V_CHANGE_04, V_CHANGE_03, V_CHANGE_02};

// 每次放电次数
const int V_TIME[6] = {1000, 200, 100, 80, 100, 200};

// 目标电压
int target_v = V_CHANGE_01;

// 一秒放电多少次
// 注，过高的放电频率会使电容来不及充电
int TIME = 30;

// ADC比例系数
float ADC_B = 0.026;

// PWM初始频率
int F = F_B;

// 检测当前电压
float V = 0;

// 是否接近目标值
uint8_t target_flag = 0;

// 记录第一次放电还是第二次放电
volatile int first_flag = 0;

// 上一次的目标值
int before_v = V_CHANGE_01;

// 串口缓冲区大小
#define UART_BUF_SIZE (1024)

// PWM结构体
ledc_timer_config_t ledc_timer = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = 8,
    .timer_num = LEDC_TIMER_0,
    .freq_hz = F_B};

ledc_channel_config_t ledc_channel = {
    .channel = LEDC_CHANNEL_0,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .gpio_num = PWM,
    .timer_sel = LEDC_TIMER_0,
    .hpoint = 0,
    .duty = 192};

// 毫秒级延迟
void delay_ms(uint32_t t)
{
    vTaskDelay(pdMS_TO_TICKS(t));
}

// GPIO初始化
void init_gpio()
{
    gpio_reset_pin(O1);
    gpio_reset_pin(O2);
    gpio_reset_pin(PWM);
    gpio_set_direction(O1, GPIO_MODE_OUTPUT);
    gpio_set_direction(O2, GPIO_MODE_OUTPUT);
    gpio_set_direction(PWM, GPIO_MODE_OUTPUT);
    gpio_set_level(O1, 0);
    gpio_set_level(O2, 0);
}

// 串口初始化
void init_uart()
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_driver_install(UART_NUM_0, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);
}

// PWM初始化
void init_pwm(void)
{
    ledc_timer_config(&ledc_timer);
    ledc_channel_config(&ledc_channel);
}

// ADC初始化
void init_adc(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_GPIO, ADC_ATTEN_DB_11);
}

// 获取ADC
void get_adc(void *arg)
{
    while (1)
    {
        // 检测是否达到目标值附近
        V = adc1_get_raw(ADC_GPIO) * ADC_B;
        int num = 0;
        if ((float)V > (float)target_v)
        {
            if ((float)V - (float)target_v >= 0.2)
            {
                num = F - 2;
                if (num >= F_B)
                {
                    F = num;
                }
            }
        }
        else if ((float)V < (float)target_v)
        {
            if ((float)target_v - (float)V >= 0.2)
            {
                num = F + 2;
                F = num;
            }
        }

        if (fabs((float)V - (float)target_v) < 0.2)
        {
            target_flag = 1;
        }
        else
        {
            target_flag = 0;
        }

        delay_ms(10);
    }
}

// pwm频率不断改变
void pwm_output(void *arg)
{
    while (1)
    {
        if (F >= F_B && target_flag == 0)
        {
            // 这里获取到被控对象的反馈值
            float feedbackValue = V;
            float targetValue = target_v;                 // 这里获取到目标值
            PID_Calc(&mypid, targetValue, feedbackValue); // 进行PID计算，结果在output成员变量中
            float output = 0;
            if (fabs(V - target_v) > 20)
            {
                output = 2.5 * mypid.output;
            }
            else
            {
                output = mypid.output;
            }

            if (F + output >= F_B)
            {
                F = F + output;
                ledc_set_freq(ledc_channel.speed_mode, ledc_channel.channel, F);
            }
        }
        delay_ms(20);
    }
}

int flag = 0;

// 日志打印的标志位
void printf_flag(void *arg)
{
    while (1)
    {
        flag = 1;
        printf("old:%d,%f,%d\n", F, V, target_v);
        delay_ms(100);
    }
}

// 重定向printf
int fputc(int ch, FILE *f)
{
    char data[1] = {ch};
    uart_write_bytes(UART_NUM_0, data, 1);
    fflush(stdout);
    return ch;
}

uint8_t five_flag = 0;
uint8_t begin_index = 0;

void five_change(void *arg)
{
    while (1)
    {
        delay_ms(CHANGE_TIME * 1000);
        five_flag = 1;
        uint8_t tmp = begin_index + 1;
        begin_index = tmp % 6;
        target_v = V_CHANGE[begin_index];
    }
}

void beat_task(void *arg)
{
    while (1)
    {
        if (first_flag == 0)
        {
            gpio_set_level(O1, 1);
            gpio_set_level(O2, 0);
            // 放电
            esp_rom_delay_us(OUTPUT_TIME);
            gpio_set_level(O1, 0);
            gpio_set_level(O2, 0);
            delay_ms((1000 / V_TIME[begin_index]));
            first_flag = 1;
        }
        else
        {
            gpio_set_level(O1, 0);
            gpio_set_level(O2, 1);
            esp_rom_delay_us(OUTPUT_TIME);
            gpio_set_level(O1, 0);
            gpio_set_level(O2, 0);
            delay_ms((1000 / V_TIME[begin_index]));
            first_flag = 0;
        }
    }
}

void on_device_init(void){

    ESP_LOGI(TAG, "device_init");
    // init power_property, it is a int between 0 and 500
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

    init_gpio();
    init_uart();
    init_adc();
    // 初始化PWM
    init_pwm();
    // ADC采集
    xTaskCreate(get_adc, "get_adc", 1024, NULL, 10, NULL);

    xTaskCreate(pwm_output, "pwm_output", 1024, NULL, 10, NULL);

    xTaskCreate(printf_flag, "printf_flag", 10240, NULL, 10, NULL);

    xTaskCreate(five_change, "five_change", 1024, NULL, 10, NULL);
    xTaskCreate(beat_task, "beat_task", 1024, NULL, 10, NULL);

    PID_Init(&mypid, kp, ki, kd, maxIntegral, maxOutput); // 初始化PID参数

    while (1)
    {
        printf("%d,%f,%d\n", F, V, begin_index);
        delay_ms(500);
    }
    

}

void on_device_first_ready(void){
    ESP_LOGI(TAG, "device_first_ready");
}