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
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_adc_cal.h"
#include "esp_log.h"

#include "mqtt_client.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/adc.h"



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

// 升压ADC校准系数
float BOOST_ADC_K = 0.026;

// 电池电压ADC校准系数
float BAT_ADC_K = 363.2;

// 连接wifi的配置
// wifi名称、密码
#define CONFIG_ESP_WIFI_SSID "test1234"
#define CONFIG_ESP_WIFI_PASSWORD "test1234"

// 电池判定为没电和满电的阈值
float bat_small = 3.7;
float bat_max = 4.2;

// PID 参数
float pid_Kp = 0.5f;
float pid_Ki = 0.03f;
float pid_Kd = 8.0f;
float pid_dead_zone = 2.0f;
float pid_max_integral = 10.0f;
float pid_min_integral = -10.0f;
float pid_max_output = 20.0f;
float pid_min_output = -20.0f;


// 连接MQTT的配置
#define MQTT_URI "mqtt://emqx@192.168.137.1:1883"
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define MQTT_CLIENTID "test_wifi_mqtt"
#define MQTT_TOPIC "/all"


void init_wifi();
void mqtt_rec_callback(const char *json_str);
void init_gpio();
void init_uart();
void init_pwm();
void init_adc();
void init_wifi();
void init_mqtt();
void init_pid();
void printf_log(void *arg);
void get_bat_adc(void *arg);
void pwm_output(void *arg);
void output(void *arg);
void mqtt_update(void *arg);

// 硬件初始化
void on_device_init()
{
    init_gpio();
    init_uart();
    init_pwm();
    init_adc();
    init_wifi();
    init_mqtt();
    init_pid();
}

// 创建任务
void on_device_first_ready()
{
    xTaskCreate(printf_log, "printf_log", 4096, NULL, 2, NULL);
    xTaskCreate(get_bat_adc, "get_bat_adc", 4096, NULL, 2, NULL);
    xTaskCreate(pwm_output, "pwm_output", 4096, NULL, 2, NULL);
    xTaskCreate(output, "output", 4096, NULL, 2, NULL);
    xTaskCreate(mqtt_update, "mqtt_update", 4096, NULL, 2, NULL);
}


// 主逻辑
void app_main(void)
{
    on_device_init();
    on_device_first_ready();
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

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

// 失败之后的重连次数
#define EXAMPLE_ESP_MAXIMUM_RETRY  5

#define CONFIG_ESP_WIFI_PW_ID ""

#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi and mqtt:";

static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


// wifi sta模式
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
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


// mqtt连接错误
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
        // 重连wifi
        init_wifi();
    }
}


// mqtt处理事件
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        // msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, MQTT_TOPIC, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        // msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        // msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        // ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        // ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        // msg_id = esp_mqtt_client_publish(client, MQTT_TOPIC, "test_wifi_mqtt connected", 0, 0, 0);
        // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        if (strncmp(event->topic,MQTT_TOPIC, event->topic_len)==0)
        {
            mqtt_rec_callback(event->data);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

// mqtt 连接
esp_mqtt_client_handle_t client;
static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URI,
        .credentials.username = MQTT_USERNAME,
        .credentials.client_id = MQTT_CLIENTID,
        .credentials.authentication.password = MQTT_PASSWORD
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
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


// wifi连接初始化
void init_wifi()
{
     //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
}

// mqtt连接
void init_mqtt()
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", (int)esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    mqtt_app_start();
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


uint8_t bat_value = 0;
// 获取当前电池百分比
void get_bat_adc(void *arg)
{
    while (1)
    {
        float bat_v = ((float)(adc1_get_raw(BAT_ADC)))/BAT_ADC_K;
        if (bat_v<=bat_small)
        {
            bat_value = 1;
        }
        else if (bat_v>=bat_max)
        {
            bat_value = 100;
        }
        else{
            bat_value = (bat_v-bat_small)/(bat_max-bat_small);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}


// pwm频率不断改变
void pwm_output(void *arg)
{
    while (1)
    {
        if (v_open_close_flag == 1)
        {
            float error = target_v - now_v;
            // 如果误差在死区范围内，则不做控制输出
            if (fabs(error) > pid.dead_zone)
            {
                if (pwm_f >= 100 && pwm_f<=15000)
                {
                    int32_t pid_output = pwm_f + PID_Compute(&pid, target_v, now_v);
                    if (pid_output >= 100 && pid_output<=15000)
                    {
                        pwm_f = pid_output;
                        ledc_set_freq(ledc_channel.speed_mode, ledc_channel.channel, pwm_f);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}


static __inline void output_delay()
{
    int ts = 25000;
    while (ts--)
    {
        __asm__ __volatile__ ("nop");
    } 
}

// 放电
void output(void *arg)
{
    uint8_t first_flag = 0;
    while (1)
    {
        if (v_open_close_flag==1)
        {
            if (first_flag == 0)
            {
                gpio_set_level(O1, 0);
                gpio_set_level(O2, 1);
                // 放电
                output_delay();
                gpio_set_level(O1, 0);
                gpio_set_level(O2, 0);
                first_flag = 1;
            }
            else
            {
                gpio_set_level(O1, 1);
                gpio_set_level(O2, 0);
                output_delay();
                gpio_set_level(O1, 0);
                gpio_set_level(O2, 0);
                first_flag = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(v_delay));
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
        vTaskDelay(pdMS_TO_TICKS(1000));
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


// 每一定时间上传数据
void mqtt_update(void *arg)
{
    char json_str[256];
    while (1)
    {
        // 调用 generate_json 函数，传入动态的值
	    generate_json(json_str, target_v,v_delay , v_open_close_flag, bat_value);

        // 打印生成的 JSON 字符串
        printf("%s\n", json_str);

        esp_mqtt_client_publish(client, MQTT_TOPIC, json_str, 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(V_UPDATE_TIME * 1000));
    }
}
