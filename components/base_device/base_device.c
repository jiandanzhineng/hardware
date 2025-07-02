#include "base_device.h"
#include "dianji.h"

#include "esp_log.h"
#include "iot_button.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "device_common.h"
#include "esp_sleep.h"

#include "esp_heap_caps.h"

#include "Battery.h"

static const char *TAG = "base_device";


// property list
device_property_t device_type_property;
device_property_t sleep_time_property;
device_property_t battery_property;
extern device_property_t *device_properties[];


long long last_msg_time = 0;

#ifdef CONNECTED_LED
static esp_timer_handle_t led_blink_timer = NULL;
static esp_timer_handle_t led_off_timer = NULL;
static bool led_blink_active = false;
#endif


#ifdef CONNECTED_LED
static void led_blink_callback(void* arg)
{
    static bool led_state = false;
    if (led_blink_active) {
        led_state = !led_state;
        gpio_set_level(CONNECTED_LED, led_state ? 0 : 1);  // 低电平有效，所以反转
    }
}

static void led_off_callback(void* arg)
{
    led_blink_active = false;
    gpio_set_level(CONNECTED_LED, 1);  // 关闭LED（高电平）
    if (led_blink_timer) {
        esp_timer_stop(led_blink_timer);
    }
}

static void led_init(void)
{
    gpio_reset_pin(CONNECTED_LED);
    gpio_set_direction(CONNECTED_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(CONNECTED_LED, 1);  // 初始状态关闭（高电平）
}

static void led_start_blink(void)
{
    if (led_blink_timer == NULL) {
        const esp_timer_create_args_t blink_timer_args = {
            .callback = &led_blink_callback,
            .name = "led_blink"
        };
        esp_timer_create(&blink_timer_args, &led_blink_timer);
    }
    
    led_blink_active = true;
    esp_timer_start_periodic(led_blink_timer, 1000000);  // 每秒闪烁一次
}

static void led_constant_on_then_off(void)
{
    // 停止闪烁
    led_blink_active = false;
    if (led_blink_timer) {
        esp_timer_stop(led_blink_timer);
    }
    
    // 常亮
    gpio_set_level(CONNECTED_LED, 0);  // 低电平有效，点亮LED
    
    // 创建10秒后关闭的定时器
    if (led_off_timer == NULL) {
        const esp_timer_create_args_t off_timer_args = {
            .callback = &led_off_callback,
            .name = "led_off"
        };
        esp_timer_create(&off_timer_args, &led_off_timer);
    }
    
    esp_timer_start_once(led_off_timer, 10000000);  // 10秒后关闭
}
#endif

void device_first_ready(void)
{
    ESP_LOGI(TAG, "device_first_ready");
    // start heartbeat task every 30 seconds
    xTaskCreate(heartbeat_task, "heartbeat_task", 1024 * 2, NULL, 10, NULL);

#ifdef CONNECTED_LED
    led_constant_on_then_off();
#endif

    on_device_first_ready();
    #ifndef BATTERY_CLOSE_EN
    gpio_reset_pin(BATTERY_ADC_EN);
    gpio_set_direction(BATTERY_ADC_EN, GPIO_MODE_OUTPUT);
    #endif
}

static void heartbeat_task(void)
{
    // report device_type every 30 seconds
    while(1) {
        report_all_properties();
        // print the time since boot
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void sleep_check_task(void){
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "uptime: %lld, no message time: %lld/%d", esp_timer_get_time() / 1000000, esp_timer_get_time() / 1000000 - last_msg_time, sleep_time_property.value.int_value);
        if(esp_timer_get_time() / 1000000 - last_msg_time > sleep_time_property.value.int_value){
            ESP_LOGI(TAG, "long time no message, deep sleep");
            esp_deep_sleep_start();
        }
        #ifndef BATTERY_CLOSE_EN
        uint8_t BatteryVoltagePer;
        battery_adc_get_value(&BatteryVoltagePer);
        battery_property.value.int_value = BatteryVoltagePer;
        #endif
    }

}

static void report_all_properties(void){
    ESP_LOGI(TAG, "report_all_properties num: %d", device_properties_num);
    // build json
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "method", "report");
    #ifdef DEBUG_HEAP
    char memeory_info[64];
    sprintf(memeory_info, "heap_caps_get_free_size: %d", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    cJSON_AddStringToObject(root, "memory_info", memeory_info);
    #endif
    // cJSON_AddStringToObject(root, "device_type", device_type_property.value.string_value);
    for(int i = 0; i< device_properties_num; i++){
        if(device_properties[i]->value_type == PROPERTY_TYPE_INT){
            cJSON_AddNumberToObject(root, device_properties[i]->name, device_properties[i]->value.int_value);
        }
        else if(device_properties[i]->value_type == PROPERTY_TYPE_FLOAT){
            cJSON_AddNumberToObject(root, device_properties[i]->name, device_properties[i]->value.float_value);
        }
        else if(device_properties[i]->value_type == PROPERTY_TYPE_STRING){
            cJSON_AddStringToObject(root, device_properties[i]->name, device_properties[i]->value.string_value);
        }else{
            ESP_LOGE(TAG, "device_properties[%d].value_type: %d is not supported", i, device_properties[i]->value_type);
        }
    }
    char *json_data = cJSON_Print(root);
    // publish
    esp_mqtt_client_publish(smqtt_client, publish_topic, json_data, 0, 1, 0);
    free(json_data);
    cJSON_Delete(root);
}

void device_init(void)
{
    ESP_LOGI(TAG, "device_init");
    
#ifdef CONNECTED_LED
    led_init();
    led_start_blink();
#endif

    // init device_type, it is a string DEVICE_TYPE
    device_type_property.readable = true;
    device_type_property.writeable = false;
    strcpy(device_type_property.name, "device_type");
    device_type_property.value_type = PROPERTY_TYPE_STRING;
    strcpy(device_type_property.value.string_value, DEVICE_TYPE_NAME);

    sleep_time_property.readable = true;
    sleep_time_property.writeable = true;
    strcpy(sleep_time_property.name, "sleep_time");
    sleep_time_property.value_type = PROPERTY_TYPE_INT;
    sleep_time_property.value.int_value = 7200;

    battery_property.readable = true;
    battery_property.writeable = false;
    strcpy(battery_property.name, "battery");
    battery_property.value_type = PROPERTY_TYPE_INT;
    battery_property.value.int_value = 0;

    xTaskCreate(sleep_check_task, "sleep_check_task", 1024 * 2, NULL, 10, NULL);

    on_device_init();

    //esp_log_level_set("*", ESP_LOG_NONE);
    
}


void mqtt_msg_process(char *topic, int topic_len, char *data, int data_len)
{
    ESP_LOGI(TAG, "mqtt_msg_process topic: %.*s, data: %.*s", topic_len, topic, data_len, data);
    // convert data to json
    cJSON *root = cJSON_Parse(data);
    int msg_id = -1;
    if (cJSON_GetObjectItem(root, "msg_id"))
    {
        msg_id = cJSON_GetObjectItem(root, "msg_id")->valueint;
    }
    if (root == NULL)
    {
        ESP_LOGE(TAG, "cJSON_Parse error");
        return;
    }
    // get method
    if (cJSON_GetObjectItem(root, "method"))
    {
        char *method = cJSON_GetObjectItem(root, "method")->valuestring;
        //ESP_LOGI(TAG, "method: %s", method);
        // if method is set, then set property
        if (strcmp(method, "set") == 0)
        {
            // get key
            if (cJSON_GetObjectItem(root, "key"))
            {
                char *key = cJSON_GetObjectItem(root, "key")->valuestring;
                //ESP_LOGI(TAG, "key: %s", key);
                // get value
                if (cJSON_GetObjectItem(root, "value"))
                {
                    cJSON *value = cJSON_GetObjectItem(root, "value");

                    // set property
                    set_property(key, value, msg_id);
                }
            }
            // if method is get, then get property
        }
        else if (strcmp(method, "get") == 0)
        {
            // get key
            if (cJSON_GetObjectItem(root, "key"))
            {
                char *key = cJSON_GetObjectItem(root, "key")->valuestring;
                //ESP_LOGI(TAG, "key: %s", key);
                // get property
                get_property(key, msg_id);
            }
        }else if(strcmp(method, "update") == 0){
            // update property in root`s keys
            cJSON *child = root->child;
            while(child != NULL){
                ESP_LOGI(TAG, "update property: %s", child->string);
                set_property(child->string, child, msg_id);
                child = child->next;
            }
        }
        else{
            // do action
            on_action(root);
        }
    }
    on_mqtt_msg_process(topic, root);
    // free cJSON
    cJSON_Delete(root);
    last_msg_time = esp_timer_get_time() / 1000000;
}

void set_property(char *property_name, cJSON *property_value, int msg_id)
{
    char *json_string = cJSON_Print(property_value);
    ESP_LOGI(TAG, "set_property property_name: %s, property_value: %s", property_name, json_string);
    free(json_string);
    device_property_t *property = NULL;
    for(int i = 0; i < device_properties_num; i++){
        if(strcmp(property_name, device_properties[i]->name) == 0){
            property = device_properties[i];
            break;
        }
    }
    if(property == NULL){
        ESP_LOGE(TAG, "property_name: %s is not supported", property_name);
        return;
    }
    
    if (property->value_type == PROPERTY_TYPE_INT)
    {
        property->value.int_value = property_value->valueint;
    }
    else if (property->value_type == PROPERTY_TYPE_FLOAT)
    {
        property->value.float_value = property_value->valuedouble;
    }
    else if (property->value_type == PROPERTY_TYPE_STRING)
    {
        strcpy(property->value.string_value, property_value->valuestring);
    }
    else
    {
        ESP_LOGE(TAG, "property->value_type: %d is not supported", property->value_type);
        return;
    }
    on_set_property(property_name, property_value, msg_id);
}

void get_property(char *property_name, int msg_id)
{
    ESP_LOGI(TAG, "get_property property_name: %s", property_name);
    // if property_name is device_type, then publish device_type
    device_property_t *property = NULL;
    for(int i = 0; i < device_properties_num; i++){
        if(strcmp(property_name, device_properties[i]->name) == 0){
            property = device_properties[i];
            break;
        }
    }
    if(property == NULL){
        ESP_LOGE(TAG, "property_name: %s is not supported", property_name);
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "method", "update");
    cJSON_AddNumberToObject(root, "msg_id", msg_id);
    cJSON_AddStringToObject(root, "key", property->name);
    if (property->value_type == PROPERTY_TYPE_INT)
    {
        cJSON_AddNumberToObject(root, "value", property->value.int_value);
    }
    else if (property->value_type == PROPERTY_TYPE_FLOAT)
    {
        cJSON_AddNumberToObject(root, "value", property->value.float_value);
    }
    else if (property->value_type == PROPERTY_TYPE_STRING)
    {
        cJSON_AddStringToObject(root, "value", property->value.string_value);
    }
    char *json_str = cJSON_Print(root);
    ESP_LOGI(TAG, "json_str: %s", json_str);
    esp_mqtt_client_publish(smqtt_client, publish_topic, json_str, 0, 1, 0);
    // release memory
    cJSON_Delete(root);
    free(json_str);
}

void mqtt_publish(cJSON *root){
    char *json_data = cJSON_Print(root);
    ESP_LOGI(TAG, "json_data: %s", json_data);
    esp_mqtt_client_publish(smqtt_client, publish_topic, json_data, 0, 1, 0);
    cJSON_Delete(root);
    free(json_data);
}