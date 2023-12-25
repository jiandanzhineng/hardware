#include "TD01.h"
#include "single_device_common.h"
#include "esp_log.h"
#include "iot_button.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "device_common.h"

#define DIMMABLE_GPIO_0 GPIO_NUM_6
#define DIMMABLE_GPIO_1 GPIO_NUM_7

#define ONOFF_GPIO_0 GPIO_NUM_2
#define ONOFF_GPIO_1 GPIO_NUM_3

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (5) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (4095) // Set duty to 50%. ((2 ** 13) - 1) * 50% = 4095
#define LEDC_FREQUENCY          (5000) // Frequency in Hertz. Set frequency at 5 kHz

#define GPIO_INPUT_TOUCH_SENSER     GPIO_NUM_4
#define GPIO_INPUT_PIN_SEL  (1ULL<<GPIO_INPUT_TOUCH_SENSER) 

#define ESP_INTR_FLAG_DEFAULT 0

static const char *TAG = "td01";

device_property_t power1_property;
device_property_t power2_property;
extern device_property_t device_type_property;
extern device_property_t sleep_time_property;

device_property_t *device_properties[] = {
    &device_type_property,
    &power1_property,
    &power2_property,
    &sleep_time_property,
};

int device_properties_num = sizeof(device_properties) / sizeof(device_properties[0]);

void button_single_click_cb(void *arg,void *usr_data)
{
    ESP_LOGI(TAG, "BUTTON_SINGLE_CLICK");
    // publish mqtt message {'method':'action', 'action':'key_boot_clicked'}
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "method", "action");
    cJSON_AddStringToObject(root, "action", "key_boot_clicked");
    char *json_data = cJSON_Print(root);
    ESP_LOGI(TAG, "json_data: %s", json_data);
    esp_mqtt_client_publish(smqtt_client, publish_topic, json_data, 0, 1, 0);
    cJSON_Delete(root);
    free(json_data);
}

void on_mqtt_msg_process(char *topic, int topic_len, char *data, int data_len){

}
void on_set_property(char *property_name, cJSON *property_value, int msg_id){
    ESP_LOGI(TAG, "on_set_property property_name:%s", property_name);
    if(strcmp(property_name, "power1") == 0){
        control_ledc(LEDC_CHANNEL_0, power1_property.value.int_value);
    }else if(strcmp(property_name, "power2") == 0){
        control_ledc(LEDC_CHANNEL_1, power2_property.value.int_value);    
    }
}

void on_device_init(void){
    ESP_LOGI(TAG, "on_device_init num:%d",device_properties_num);
        // init power1_property, it is a int between 0 and 255
    power1_property.readable = true;
    power1_property.writeable = true;
    strcpy(power1_property.name, "power1");
    power1_property.value_type = PROPERTY_TYPE_INT;
    power1_property.value.int_value = 0;

    // init power2_property, it is a int between 0 and 255
    power2_property.readable = true;
    power2_property.writeable = true;
    strcpy(power2_property.name, "power2");
    power2_property.value_type = PROPERTY_TYPE_INT;
    power2_property.value.int_value = 0;

        // create gpio button
    button_config_t gpio_btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = 9,
            .active_level = 0,
        },
    };
    button_handle_t gpio_btn = iot_button_create(&gpio_btn_cfg);
    if(NULL == gpio_btn) {
        ESP_LOGE(TAG, "Button create failed");
    }
    iot_button_register_cb(gpio_btn, BUTTON_SINGLE_CLICK, button_single_click_cb,NULL);

    dimmable_plug_pwm_init();
}

void on_device_first_ready(void){
}
void on_action(cJSON *root){

}

void control_ledc(ledc_channel_t channel, uint32_t duty)
{
    ESP_LOGI(TAG, "control_ledc CHANNEL: %d DUTY: %ld", channel, duty);
    
    if(duty<2)duty=0;
    else if(duty>254)duty = 8191;
    else{
        duty = duty * 9 + 5897;
    }
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, channel, duty));
    // Update duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, channel));
}

void dimmable_plug_pwm_init(void)
{
    ESP_LOGI(TAG, "dimmable_plug_pwm_init");
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .gpio_num       = DIMMABLE_GPIO_0,
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    ledc_channel_config_t ledc_channel1 = {
        .gpio_num       = DIMMABLE_GPIO_1,
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_1,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel1));
    control_ledc(LEDC_CHANNEL_0, 0);
    control_ledc(LEDC_CHANNEL_1, 0);
}
