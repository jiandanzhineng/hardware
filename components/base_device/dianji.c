#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "mqtt_client.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/adc.h"

#include "cJSON.h"

#include "base_device.h"
#include "dianji.h"
#include "device_common.h"


static const char *TAG = "dianji";


device_property_t voltage_property;
device_property_t delay_property;
device_property_t shock_property;
extern device_property_t device_type_property;
extern device_property_t sleep_time_property;
extern device_property_t battery_property;

device_property_t *device_properties[] = {
    &device_type_property,
    &sleep_time_property,
    &voltage_property,
    &battery_property,
    &delay_property,
    &shock_property,
};

int device_properties_num = sizeof(device_properties) / sizeof(device_properties[0]);


// 板载LED指示灯引脚
#define BZ_LED1 12
#define BZ_LED2 13

// 通过两个输出引脚控制放电
#define O1 3
#define O2 19

// PWM boost升压控制引脚
#define BOOST_PWM 10

// 升压ADC检测引脚
#define BOOST_ADC 4

// 串口缓冲区大小
#define UART_BUF_SIZE (512)

// 电池ADC检测使能引脚
#define BAT_ADC_EN 18

// 电池ADC检测引脚
#define BAT_ADC 1

// 每60s 上传mqtt数据
#define V_UPDATE_TIME 60

// PWM结构体
ledc_timer_config_t ledc_timer = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = 8,
    .timer_num = LEDC_TIMER_0,
    .freq_hz = 100};

ledc_channel_config_t ledc_channel = {
    .channel = LEDC_CHANNEL_0,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .gpio_num = BOOST_PWM,
    .timer_sel = LEDC_TIMER_0,
    .hpoint = 0,
    .duty = 192};

// 升压ADC校准系数
float BOOST_ADC_K = 0.026;

// 电池电压ADC校准系数
float BAT_ADC_K = 363.2;

// PID 参数
float pid_Kp = 2.0f;
float pid_Ki = 0.05f;
float pid_Kd = 8.0f;
float pid_dead_zone = 2.0f;
float pid_max_integral = 25.0f;
float pid_min_integral = -25.0f;
float pid_max_output = 50.0f;
float pid_min_output = -50.0f;

// 目标电压
float target_v = 0;

// PWM频率
int32_t pwm_f = 100;

// 当前实际电压
float now_v = 0;

// 放电的时间间隔(多少毫秒放一下电)
int32_t v_delay = 30;

// 是否放电
uint8_t v_open_close_flag = 0;

// 停止电击计时
int32_t remain_time = 0;


void mqtt_rec_callback(const char *json_str);
void init_gpio();
void init_uart();
void init_pwm();
void init_adc();
void init_pid();
void init_timer();
void printf_log(void *arg);
void get_bat_adc(void *arg);
void pwm_output(void *arg);
void output(void *arg);
void mqtt_update(void *arg);
void init_property();
void stop_shock_task();


// 硬件初始化
void on_device_init()
{
    init_gpio();
    init_uart();
    init_pwm();
    init_adc();
    init_pid();
    init_timer();
	init_property();
}

// 创建任务
void on_device_first_ready()
{
    xTaskCreate(printf_log, "printf_log", 4096, NULL, 2, NULL);
    xTaskCreate(get_bat_adc, "get_bat_adc", 4096, NULL, 2, NULL);
    xTaskCreate(pwm_output, "pwm_output", 4096, NULL, 2, NULL);
    xTaskCreate(output, "output", 4096, NULL, 2, NULL);
    //xTaskCreate(mqtt_update, "mqtt_update", 4096, NULL, 2, NULL);
    xTaskCreate(stop_shock_task, "stop_shock_task", 4096, NULL, 2, NULL);
}

void on_set_property(char *property_name, cJSON *property_value, int msg_id)
{
    //ESP_LOGI(TAG, "on_set_property property_name:%s", property_name);
    // if(strcmp(property_name, "power1") == 0){
    //     control_ledc(LEDC_CHANNEL_0, power1_property.value.int_value);
    // }else if(strcmp(property_name, "power2") == 0){
    //     control_ledc(LEDC_CHANNEL_1, power2_property.value.int_value);    
    // }
    // if(strcmp(property_name, "shock") == 0){
    //     v_open_close_flag = shock_property.value.int_value;
    //     // if (v_open_close_flag==0)
    //     // {
    //     //     ledc_stop(ledc_channel.speed_mode, ledc_channel.channel, 0);
    //     // }
    //     // else
    //     // {
    //     //     init_pwm();
    //     // }
    // }
}

void on_action(cJSON *root)
{
    char *method = cJSON_GetObjectItem(root, "method")->valuestring;
    if (strcmp(method, "dian") == 0)
    {
        int time = cJSON_GetObjectItem(root, "time")->valueint;
        int voltage = cJSON_GetObjectItem(root, "voltage")->valueint;
        voltage_property.value.int_value = voltage;
        shock_property.value.int_value = 1;
        ESP_LOGI(TAG, "dian time: %d, voltage: %d", time, voltage);
        remain_time = time;
        // vTaskDelay(time / portTICK_PERIOD_MS);
        // shock_property.value.int_value = 0;
        // // control_ledc(LEDC_CHANNEL, (uint32_t)0);
        // ESP_LOGI(TAG, "dian end");
    }
}

void stop_shock_task()
{
    while (1)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        if (remain_time > 100)
        {
            remain_time -= 100;
        }
        else if (remain_time > 0)
        {
            shock_property.value.int_value = 0;
            remain_time = 0;
        }
    }
    
}




// json数据解析
void trim_spaces(char* str) {
    char* end;

    // Trim leading spaces
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }

    // Trim trailing spaces
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }

    // Null-terminate the string
    *(end + 1) = '\0';
}


void init_property()
{
	ESP_LOGI(TAG, "device_init");
    // init power_property, it is a int between 0 and 500
    voltage_property.readable = true;
    voltage_property.writeable = true;
    strcpy(voltage_property.name, "voltage");
    voltage_property.value_type = PROPERTY_TYPE_INT;
    voltage_property.value.int_value = 0;
    voltage_property.max = 100;
    voltage_property.min = 0;

    // init delay_property, it is a int between 100 and 1000
    delay_property.readable = true;
    delay_property.writeable = true;
    strcpy(delay_property.name, "delay");
    delay_property.value_type = PROPERTY_TYPE_INT;
    delay_property.value.int_value = 30;
    delay_property.max = 1000;
    delay_property.min = 20;

    shock_property.readable = true;
    shock_property.writeable = true;
    strcpy(shock_property.name, "shock");
    shock_property.value_type = PROPERTY_TYPE_INT;
    shock_property.value.int_value = 0;
    shock_property.max = 1;
    shock_property.min = 0;
}

// 函数: 解析 JSON 并提取字段值
int parse_json(const char* json_str, const char* key, char* value, int* is_number) {
    char* start;
    char* end;
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);

    // 查找键的位置
    start = strstr(json_str, search_key);
    if (start == NULL) {
        return -1;  // 未找到键
    }

    // 跳过键和值之间的 ":"
    start += strlen(search_key);

    // 跳过可能存在的空格
    while (*start == ' ' || *start == '\t') {
        start++;
    }

    // 如果值是字符串类型
    if (*start == '\"') {
        start++;  // 跳过开头的引号
        end = strchr(start, '\"');
        if (end == NULL) {
            return -1;  // 格式错误：缺少闭合引号
        }

        // 提取字符串值
        strncpy(value, start, end - start);
        value[end - start] = '\0';

        *is_number = 0;  // 标记这是一个字符串
    } else {  // 如果值是数字类型
        end = strchr(start, ',');
        if (end == NULL) {
            end = strchr(start, '}');
        }

        if (end == NULL) {
            return -1;  // 格式错误：缺少闭合大括号
        }

        // 提取数字值（数字字符串）
        strncpy(value, start, end - start);
        value[end - start] = '\0';

        *is_number = 1;  // 标记这是一个数字
    }

    return 0;
}


void on_mqtt_msg_process(char *topic, cJSON *root)
{
    // 将 cJSON 对象转换为字符串
    char *json_str = cJSON_PrintUnformatted(root);  // 或者使用 cJSON_Print(root) 如果需要格式化输出

    if (json_str != NULL) {
        // 调用 mqtt_rec_callback 函数，传入 JSON 字符串
        //mqtt_rec_callback(json_str);

        // 记得在不再需要 json_str 时释放内存
        cJSON_free(json_str);
    } else {
        // 处理转换失败的情况
        printf("Error: cJSON_PrintUnformatted failed\n");
    }
}


// PID控制
typedef struct
{
	float Kp;              // 比例系数
	float Ki;              // 积分系数
	float Kd;              // 微分系数
	float prev_error;      // 上一次误差
	float prev_prev_error; // 上两次误差
	float integral;        // 积分值
	float output;          // 控制输出
	float dead_zone;       // 死区阈值
	float max_integral;    // 积分部分的最大限制
	float min_integral;    // 积分部分的最小限制
	float max_output;      // 控制输出的最大值
	float min_output;      // 控制输出的最小值
} PIDController;


// 初始化 PID 控制器
void PID_Init(PIDController *pid, float Kp, float Ki, float Kd, float dead_zone,
              float max_integral, float min_integral, float max_output, float min_output)
{
	pid->Kp = Kp;
	pid->Ki = Ki;
	pid->Kd = Kd;
	pid->dead_zone = dead_zone;
	pid->max_integral = max_integral;
	pid->min_integral = min_integral;
	pid->max_output = max_output;
	pid->min_output = min_output;

	pid->prev_error = 0.0f;
	pid->prev_prev_error = 0.0f;
	pid->integral = 0.0f;
	pid->output = 0.0f;
}

// PID 输出
float PID_Compute(PIDController *pid, float setpoint, float measured_value) {
    if(setpoint > 100){
        setpoint = 100;
    }
    // 当前误差
    float error = setpoint - measured_value;
    // 积分部分
    pid->integral += error;
    // 限制积分部分，防止积分风暴
    if (pid->integral > pid->max_integral) {
        pid->integral = pid->max_integral;
    } else if (pid->integral < pid->min_integral) {
        pid->integral = pid->min_integral;
    }

    // 微分部分（计算误差变化率）
    float derivative = error - pid->prev_error;

    // 计算 PID 输出
    pid->output = pid->Kp * error + pid->Ki * pid->integral + pid->Kd * derivative;

    // 限制控制输出范围
    if (pid->output > pid->max_output) {
        pid->output = pid->max_output;
    } else if (pid->output < pid->min_output) {
        pid->output = pid->min_output;
    }

    // 更新误差历史记录
    pid->prev_error = error;

    return pid->output;
}





// PWM初始化
void init_pwm(void)
{
    ledc_timer_config(&ledc_timer);
    ledc_channel_config(&ledc_channel);
    // ledc_stop(ledc_channel.speed_mode, ledc_channel.channel, 0);
}



// pid参数初始化
PIDController pid;
void init_pid(void)
{
    PID_Init(&pid, pid_Kp, pid_Ki, pid_Kd, pid_dead_zone, pid_max_integral, pid_min_integral, pid_max_output, pid_min_output);
}


// mqtt接收回调
void mqtt_rec_callback(const char* json_str)
{
    char value[256];
    int is_number;
    // 解析 "method" 字段
    if (parse_json(json_str, "method", value, &is_number) == 0) {
        if (is_number) {
            printf("method: (number) %s\n", value);
        } else {
            printf("method: (string) %s\n", value);
        }
    } else {
        printf("Error parsing 'method'\n");
    }

    // 解析 "voltage" 字段
    if (parse_json(json_str, "voltage", value, &is_number) == 0) {
        if (is_number) {
            int voltage = atoi(value);  // 将数字字符串转换为整数
            target_v = voltage;
            printf("voltage: %d\n", voltage);
        } else {
            printf("voltage: (string) %s\n", value);
        }
    } else {
        printf("Error parsing 'voltage'\n");
    }

    // 解析 "delay" 字段
    if (parse_json(json_str, "delay", value, &is_number) == 0) {
        if (is_number) {
            int delay = atoi(value);  // 将数字字符串转换为整数
            v_delay =  delay;
            printf("delay: %d\n", delay);
        } else {
            printf("delay: (string) %s\n", value);
        }
    } else {
        printf("Error parsing 'delay'\n");
    }

    // 解析 "shock" 字段
    if (parse_json(json_str, "shock", value, &is_number) == 0) {
        if (is_number) {
            int shock = atoi(value);  // 将数字字符串转换为整数
            v_open_close_flag = shock;
            if (v_open_close_flag==0)
            {
                ledc_stop(ledc_channel.speed_mode, ledc_channel.channel, 0);
            }
            else
            {
                init_pwm();
            }
            
            printf("shock: %d\n", shock);
        } else {
            printf("shock: (string) %s\n", value);
        }
    } else {
        printf("Error parsing 'shock'\n");
    }
    // 下面是pid参数，方便后续调参
    // 解析 "pid_p" 字段
    if (parse_json(json_str, "pid_p", value, &is_number) == 0) {
        if (is_number) {
            int pid_p = atoi(value);  // 将数字字符串转换为整数
            pid.Kp = ((float)pid_p)/10000;
            printf("pid_p: %d\n", pid_p);
        } else {
            printf("pid_p: (string) %s\n", value);
        }
    } else {
        printf("Error parsing 'pid_p'\n");
    }

    // 解析 "pid_i" 字段
    if (parse_json(json_str, "pid_i", value, &is_number) == 0) {
        if (is_number) {
            int pid_i = atoi(value);  // 将数字字符串转换为整数
             pid.Ki = ((float)pid_i)/10000;
            printf("pid_i: %d\n", pid_i);
        } else {
            printf("pid_i: (string) %s\n", value);
        }
    } else {
        printf("Error parsing 'pid_i'\n");
    }

    // 解析 "pid_d" 字段
    if (parse_json(json_str, "pid_d", value, &is_number) == 0) {
        if (is_number) {
            int pid_d = atoi(value);  // 将数字字符串转换为整数
            pid.Kd = ((float)pid_d)/10000;
            printf("pid_d: %d\n", pid_d);
        } else {
            printf("pid_d: (string) %s\n", value);
        }
    } else {
        printf("Error parsing 'pid_d'\n");
    }
}




// GPIO初始化
void init_gpio()
{
    gpio_reset_pin(O1);
    gpio_reset_pin(O2);
    gpio_reset_pin(BOOST_PWM);
    gpio_reset_pin(BZ_LED1);
    gpio_reset_pin(BZ_LED2);
    gpio_reset_pin(BAT_ADC_EN);
    gpio_reset_pin(BOOST_ADC);
    gpio_reset_pin(BAT_ADC);

    gpio_set_direction(O1, GPIO_MODE_OUTPUT);
    gpio_set_direction(O2, GPIO_MODE_OUTPUT);
    gpio_set_direction(BOOST_PWM, GPIO_MODE_OUTPUT);
    gpio_set_direction(BZ_LED1, GPIO_MODE_OUTPUT);
    gpio_set_direction(BZ_LED2, GPIO_MODE_OUTPUT);
    gpio_set_direction(BAT_ADC_EN, GPIO_MODE_OUTPUT);

    gpio_set_level(O1, 0);
    gpio_set_level(O2, 0);
    gpio_set_level(BZ_LED1, 0);
    gpio_set_level(BZ_LED2, 0);
    gpio_set_level(BAT_ADC_EN, 1);
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


// ADC初始化
void init_adc(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BOOST_ADC, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(BAT_ADC, ADC_ATTEN_DB_11);
}


// 获取升压ADC
void get_adc(void *arg)
{
    while (1)
    {
        now_v = adc1_get_raw(BOOST_ADC) * BOOST_ADC_K;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


// 函数：根据电压值返回对应的百分比
int get_battery_percentage(float bat_v) {
    if (bat_v >= 4.20) return 100;
    if (bat_v >= 4.06) return 90;
    if (bat_v >= 3.98) return 80;
    if (bat_v >= 3.92) return 70;
    if (bat_v >= 3.87) return 60;
    if (bat_v >= 3.82) return 50;
    if (bat_v >= 3.79) return 40;
    if (bat_v >= 3.77) return 30;
    if (bat_v >= 3.74) return 20;
    if (bat_v >= 3.68) return 10;
    if (bat_v >= 3.45) return 5;
    if (bat_v >= 3.00) return 0;
    
    return 0;  // 电压低于3.00V时，返回0%
}

uint8_t bat_value = 0;
// 获取当前电池百分比
void get_bat_adc(void *arg)
{
    while (1)
    {
        float bat_v = ((float)(adc1_get_raw(BAT_ADC)))/BAT_ADC_K;
        bat_value = get_battery_percentage(bat_v);
		battery_property.value.int_value = bat_value;
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}


// pwm频率不断改变
void pwm_output(void *arg)
{
    while (1)
    {
        target_v = voltage_property.value.int_value;
        float error = target_v - now_v;
        // 如果误差在死区范围内，则不做控制输出
        if (fabs(error) > pid.dead_zone)
        {
            if (pwm_f >= 100 && pwm_f<=20000)
            {
                int32_t pid_output = pwm_f + PID_Compute(&pid, target_v, now_v);
                if (pid_output >= 100 && pid_output<=20000)
                {
                    pwm_f = pid_output;
                    ledc_set_freq(ledc_channel.speed_mode, ledc_channel.channel, pwm_f);
                }
            }
        }
        if (shock_property.value.int_value != 1)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}




// 创建定时器
esp_timer_handle_t timer;
// 定时器回调函数
void timer_callback(void* arg)
{
    gpio_set_level(O1, 0);
    gpio_set_level(O2, 0);
    esp_timer_stop(timer);
}

void init_timer()
{
    // 创建一个esp_timer_create_args_t结构体，设置定时器参数
    esp_timer_create_args_t timer_args = {
        .callback = timer_callback,                 // 设置回调函数
        .name = "my_timer",                          // 设置定时器名称
        .dispatch_method = ESP_TIMER_TASK,           // 从任务中调用回调函数
        .skip_unhandled_events = false               // 不跳过未处理的事件
    };

    
    esp_timer_create(&timer_args, &timer);

    // 启动定时器，每毫秒触发一次
    // 1000微秒
    // esp_timer_start_periodic(timer, 1000); 
}


// 放电
void output(void *arg)
{
    uint8_t first_flag = 0;
    while (1)
    {
        if (shock_property.value.int_value==1)
        {
            if (first_flag == 0)
            {
                // 放电
                gpio_set_level(O1, 0);
                gpio_set_level(O2, 1);
                // 放电1ms
                esp_timer_start_periodic(timer, 2000); 
                first_flag = 1;
            }
            else
            {
                gpio_set_level(O1, 1);
                gpio_set_level(O2, 0);
                esp_timer_start_periodic(timer, 2000); 
                first_flag = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(delay_property.value.int_value));
    }
}

// 日志打印
void printf_log(void *arg)
{
    while (1)
    {
        now_v = adc1_get_raw(BOOST_ADC) * BOOST_ADC_K;
        printf("now_v is %f\r\n",now_v);
        printf("target_v is %f\r\n",target_v);
        printf("bat_v is %f\r\n",((float)(adc1_get_raw(BAT_ADC)))/(BAT_ADC_K));
        printf("pwm_f is %d\r\n",(int)pwm_f);
        printf("pid is %f %f %f\r\n",pid.Kp,pid.Ki,pid.Kd);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}


// json上传的数据
void generate_json(char *json_str, int voltage, int delay, int shock, int battery)
{
	// 使用 sprintf 格式化 JSON 字符串并存储到 json_str 数组中
	sprintf(json_str,
	        "{\n"
	        "  \"method\": \"update\",\n"
	        "  \"voltage\": %d,\n"
	        "  \"delay\": %d,\n"
	        "  \"shock\": %d,\n"
	        "  \"battery\": %d\n"
	        "}",
	        voltage, delay, shock, battery);
}

extern char publish_topic[32];
// 每一定时间上传数据
void mqtt_update(void *arg)
{
    char json_str[256];
    while (1)
    {
        // 调用 generate_json 函数，传入动态的值
	    generate_json(json_str, target_v,v_delay , v_open_close_flag, bat_value);

        // 打印生成的 JSON 字符串
        printf("publish topic is%s\n", publish_topic);
        printf("publish data is%s\n", json_str);

        voltage_property.value.int_value = target_v;
        battery_property.value.int_value = bat_value;
        delay_property.value.int_value = v_delay;
        shock_property.value.int_value = v_open_close_flag;

        // esp_mqtt_client_publish(smqtt_client, publish_topic, json_str, 0, 1, 0);
        vTaskDelay(pdMS_TO_TICKS(V_UPDATE_TIME * 1000));
    }
}




