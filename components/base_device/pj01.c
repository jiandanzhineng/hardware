#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "mqtt_client.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#include "cJSON.h"



#include "base_device.h"
#include "device_common.h"

static const char *TAG = "pj01";

// 引脚定义
#define TURN_PIN    GPIO_NUM_7
#define LED_PIN     GPIO_NUM_10
#define PWM_PIN     GPIO_NUM_6

// PWM配置
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // 设置占空比分辨率为10位
#define LEDC_FREQUENCY          (1000) // 频率1000Hz

// 设备属性定义
device_property_t pwm_duty_property;
extern device_property_t device_type_property;
extern device_property_t sleep_time_property;
// 注意：PJ01设备没有电池，所以不包含battery_property

device_property_t *device_properties[] = {
    &device_type_property,
    &sleep_time_property,
    &pwm_duty_property,
};

int device_properties_num = sizeof(device_properties) / sizeof(device_properties[0]);

// 函数声明
void init_gpio();
void init_pwm();
void init_property();

void update_pwm_duty(int duty);

// 硬件初始化
void on_device_init()
{
    init_gpio();
    init_pwm();

    init_property();
    update_pwm_duty(1023);
}

// 创建任务
void on_device_first_ready()
{
    ESP_LOGI(TAG, "PJ01 device ready");
}

void on_set_property(char *property_name, cJSON *property_value, int msg_id)
{
    ESP_LOGI(TAG, "on_set_property property_name:%s", property_name);
    if (strcmp(property_name, "pwm_duty") == 0)
    {
        int duty = property_value->valueint;
        if (duty >= 0 && duty <= 100) {
            pwm_duty_property.value.int_value = duty;
            // 映射：0->1023, 100->0
            int actual_duty = 1023 - (duty * 1023 / 100);
            update_pwm_duty(actual_duty);
            ESP_LOGI(TAG, "PWM duty set to: %d (actual: %d)", duty, actual_duty);
        } else {
            ESP_LOGE(TAG, "Invalid PWM duty value: %d (should be 0-100)", duty);
        }
    }
}

void on_action(cJSON *root)
{
    char *method = cJSON_GetObjectItem(root, "method")->valuestring;
    ESP_LOGI(TAG, "Received action: %s", method);
    // PJ01设备暂时不需要特殊动作处理
}

void on_mqtt_msg_process(char *topic, cJSON *root)
{
    // 处理MQTT消息
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL) {
        ESP_LOGI(TAG, "Received MQTT message: %s", json_str);
        cJSON_free(json_str);
    }
}

// GPIO初始化
void init_gpio()
{
    // 配置GPIO输出引脚
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TURN_PIN) | (1ULL << LED_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    
    // 设置GPIO输出电平
    gpio_set_level(TURN_PIN, 1);
    gpio_set_level(LED_PIN, 1);
    
    ESP_LOGI(TAG, "GPIO initialized");
}

// PWM初始化
void init_pwm()
{
    // 配置PWM定时器
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    
    // 配置PWM通道
    ledc_channel_config_t ledc_channel = {
        .channel    = LEDC_CHANNEL,
        .duty       = 0,
        .gpio_num   = PWM_PIN,
        .speed_mode = LEDC_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    
    ESP_LOGI(TAG, "PWM initialized");
}

// 更新PWM占空比
void update_pwm_duty(int duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
    ESP_LOGI(TAG, "PWM duty updated to: %d", duty);
}

// 属性初始化
void init_property()
{
    ESP_LOGI(TAG, "Initializing properties");
    
    // 初始化PWM占空比属性
    pwm_duty_property.readable = true;
    pwm_duty_property.writeable = true;
    strcpy(pwm_duty_property.name, "pwm_duty");
    pwm_duty_property.value_type = PROPERTY_TYPE_INT;
    pwm_duty_property.value.int_value = 0;
    pwm_duty_property.max = 100;  // 幅度范围0-100
    pwm_duty_property.min = 0;
}



void on_device_before_sleep(void)
{
    // PJ01 device has no special cleanup before sleep
}