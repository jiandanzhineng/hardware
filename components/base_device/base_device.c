#include "base_device.h"
#include "device_serial_debug.h"

#include "esp_log.h"
#include "iot_button.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "device_common.h"
#include "esp_sleep.h"
#include "esp_ota_ops.h"
#include "ota_update.h"
#include "freertos/semphr.h"

// #include "device_ble_service.h"

#include "esp_heap_caps.h"

#include "Battery.h"

__attribute__((weak)) void device_ble_update_property(int index) {}
__attribute__((weak)) void device_ble_send_message(const char *message) {}

static const char *TAG = "base_device";
static const esp_app_desc_t *app_desc = NULL;


// property list
device_property_t device_type_property;
device_property_t sleep_time_property;
device_property_t battery_property;
extern device_property_t *device_properties[];


long long last_msg_time = 0;
volatile int g_device_first_ready_called = 0;
static SemaphoreHandle_t s_first_ready_mutex;
static SemaphoreHandle_t s_receive_mutex;
static esp_err_t s_first_ready_result = ESP_ERR_INVALID_STATE;

bool device_is_ready(void)
{
    bool ready = false;
    if (s_first_ready_mutex) {
        xSemaphoreTake(s_first_ready_mutex, portMAX_DELAY);
        ready = g_device_first_ready_called && s_first_ready_result == ESP_OK;
        xSemaphoreGive(s_first_ready_mutex);
    }
    return ready;
}

void update_last_msg_time(void){
    last_msg_time = esp_timer_get_time() / 1000000;
}

#ifdef CONNECTED_LED
static esp_timer_handle_t led_blink_timer = NULL;
static esp_timer_handle_t led_off_timer = NULL;
static bool led_blink_active = false;
static bool led_state = false;
#endif


#ifdef CONNECTED_LED
static void led_blink_callback(void* arg)
{
    if (led_blink_active) {
        led_state = !led_state;
        gpio_set_level(CONNECTED_LED, led_state ? !CONNECTED_CLOSED_LED_LEVEL : CONNECTED_CLOSED_LED_LEVEL);
    }
}

static void led_off_callback(void* arg)
{
    led_blink_active = false;
    gpio_set_level(CONNECTED_LED, CONNECTED_CLOSED_LED_LEVEL);  // 关闭LED
    if (led_blink_timer) {
        esp_timer_stop(led_blink_timer);
    }
}

static void led_init(void)
{
    gpio_reset_pin(CONNECTED_LED);
    gpio_set_direction(CONNECTED_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(CONNECTED_LED, CONNECTED_CLOSED_LED_LEVEL);  // 初始状态关闭
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
    led_state = false;
    esp_timer_stop(led_blink_timer);
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
    gpio_set_level(CONNECTED_LED, !CONNECTED_CLOSED_LED_LEVEL);  // 点亮LED
    
    // 创建10秒后关闭的定时器
    if (led_off_timer == NULL) {
        const esp_timer_create_args_t off_timer_args = {
            .callback = &led_off_callback,
            .name = "led_off"
        };
        esp_timer_create(&off_timer_args, &led_off_timer);
    }
    esp_timer_stop(led_off_timer);
    esp_timer_start_once(led_off_timer, 10000000);  // 10秒后关闭
}

static void led_fast_blink_for_three_seconds(void)
{
    if (led_blink_timer == NULL) {
        const esp_timer_create_args_t blink_timer_args = {
            .callback = &led_blink_callback,
            .name = "led_blink"
        };
        esp_timer_create(&blink_timer_args, &led_blink_timer);
    }
    if (led_off_timer == NULL) {
        const esp_timer_create_args_t off_timer_args = {
            .callback = &led_off_callback,
            .name = "led_off"
        };
        esp_timer_create(&off_timer_args, &led_off_timer);
    }

    esp_timer_stop(led_blink_timer);
    esp_timer_stop(led_off_timer);
    led_blink_active = true;
    led_state = false;
    gpio_set_level(CONNECTED_LED, !CONNECTED_CLOSED_LED_LEVEL);
    esp_timer_start_periodic(led_blink_timer, 100000);
    esp_timer_start_once(led_off_timer, 3000000);
}
#endif

static void heartbeat_task(void *arg)
{
    (void)arg;
    // report device_type every 30 seconds
    while(1) {
        device_report_all_properties();
        // print the time since boot
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

esp_err_t device_first_ready(void)
{
    TaskHandle_t heartbeat_handle = NULL;

    if (s_first_ready_mutex) {
        xSemaphoreTake(s_first_ready_mutex, portMAX_DELAY);
    }
    if(g_device_first_ready_called){
        esp_err_t result = s_first_ready_result;
        if (s_first_ready_mutex) {
            xSemaphoreGive(s_first_ready_mutex);
        }
        return result;
    }
    g_device_first_ready_called = 1;
    ESP_LOGI(TAG, "device_first_ready");

    // Do not let heartbeat report until device-specific first-ready succeeds.
    vTaskSuspendAll();
    BaseType_t heartbeat_created = xTaskCreate(
        heartbeat_task, "heartbeat_task", 1024 * 4, NULL, 10, &heartbeat_handle);
    if (heartbeat_created == pdPASS) {
        vTaskSuspend(heartbeat_handle);
    }
    xTaskResumeAll();
    if (heartbeat_created != pdPASS) {
        s_first_ready_result = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "Failed to create heartbeat task");
        goto done;
    }
    
    s_first_ready_result = on_device_first_ready();
    if (s_first_ready_result != ESP_OK) {
        vTaskDelete(heartbeat_handle);
        heartbeat_handle = NULL;
        ESP_LOGE(TAG, "Device first-ready callback failed: %s",
                 esp_err_to_name(s_first_ready_result));
        goto done;
    }
    #ifndef BATTERY_CLOSE_EN
    gpio_reset_pin(BATTERY_ADC_EN);
    gpio_set_direction(BATTERY_ADC_EN, GPIO_MODE_OUTPUT);
    #endif

    // Confirm the image and signal readiness only after all initialization succeeds.
    ota_mark_app_valid();
#ifdef CONNECTED_LED
    led_constant_on_then_off();
#endif
    vTaskResume(heartbeat_handle);

done:
    if (s_first_ready_mutex) {
        xSemaphoreGive(s_first_ready_mutex);
    }
    return s_first_ready_result;
}



static void sleep_check_task(void *arg){
    (void)arg;
    while(1) {
        #ifndef BATTERY_CLOSE_EN
        uint8_t BatteryVoltagePer;
        battery_adc_get_value(&BatteryVoltagePer);
        device_update_property_int("battery", BatteryVoltagePer);
        #endif
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "uptime: %lld, no message time: %lld/%d", esp_timer_get_time() / 1000000, esp_timer_get_time() / 1000000 - last_msg_time, sleep_time_property.value.int_value);
        if(esp_timer_get_time() / 1000000 - last_msg_time > sleep_time_property.value.int_value){
            ESP_LOGI(TAG, "long time no message, deep sleep");
            on_device_before_sleep();
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_deep_sleep_start();
        }
    }

}

void device_report_all_properties(void){
    if (g_device_mode == MODE_BLE && !device_serial_debug_is_active()) return;
    ESP_LOGI(TAG, "report_all_properties num: %d", device_properties_num);
    // build json
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create property report");
        return;
    }
    cJSON_AddStringToObject(root, "method", "report");
    if (app_desc == NULL) app_desc = esp_ota_get_app_description();
    cJSON_AddStringToObject(root, "ver", app_desc->version);
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
    device_publish_message(root);
}

void device_init(void)
{
    ESP_LOGI(TAG, "device_init");
    app_desc = esp_ota_get_app_description();

    s_first_ready_mutex = xSemaphoreCreateMutex();
    s_receive_mutex = xSemaphoreCreateRecursiveMutex();
    configASSERT(s_first_ready_mutex != NULL);
    configASSERT(s_receive_mutex != NULL);
    
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


void device_handle_action(cJSON *root)
{
    cJSON *method_item = cJSON_GetObjectItem(root, "method");
    cJSON *action_item = cJSON_GetObjectItem(root, "action");
    if (cJSON_IsString(method_item) && cJSON_IsString(action_item) &&
        strcmp(method_item->valuestring, "action") == 0 &&
        strcmp(action_item->valuestring, "blink") == 0) {
#ifdef CONNECTED_LED
        ESP_LOGI(TAG, "handle common action: blink");
        led_fast_blink_for_three_seconds();
#else
        ESP_LOGI(TAG, "common action blink ignored: CONNECTED_LED not configured");
#endif
        return;
    }

    on_action(root);
}

void device_handle_receive(char *topic, int topic_len, char *data, int data_len)
{
    if (!topic || topic_len < 0 || !data || data_len <= 0) {
        ESP_LOGW(TAG, "Invalid receive arguments");
        return;
    }
    if (!device_is_ready()) {
        ESP_LOGW(TAG, "Ignoring command before device first-ready");
        return;
    }

    if (s_receive_mutex) {
        xSemaphoreTakeRecursive(s_receive_mutex, portMAX_DELAY);
    }

    ESP_LOGI(TAG, "receive source: %.*s, data: %.*s", topic_len, topic, data_len, data);
    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(data, data_len, &parse_end, false);
    const char *data_end = data + data_len;
    while (parse_end && parse_end < data_end &&
           (*parse_end == ' ' || *parse_end == '\t' ||
            *parse_end == '\r' || *parse_end == '\n')) {
        parse_end++;
    }
    if (!root || !cJSON_IsObject(root) || parse_end != data_end)
    {
        ESP_LOGE(TAG, "Invalid JSON command");
        cJSON_Delete(root);
        if (s_receive_mutex) {
            xSemaphoreGiveRecursive(s_receive_mutex);
        }
        return;
    }
    int msg_id = -1;
    cJSON *msg_id_item = cJSON_GetObjectItem(root, "msg_id");
    if (msg_id_item && !cJSON_IsNumber(msg_id_item)) {
        ESP_LOGW(TAG, "Invalid msg_id");
        cJSON_Delete(root);
        if (s_receive_mutex) {
            xSemaphoreGiveRecursive(s_receive_mutex);
        }
        return;
    }
    if (cJSON_IsNumber(msg_id_item))
    {
        msg_id = msg_id_item->valueint;
    }
    // get method
    bool valid_command = true;
    cJSON *method_item = cJSON_GetObjectItem(root, "method");
    if (cJSON_IsString(method_item))
    {
        char *method = method_item->valuestring;
        //ESP_LOGI(TAG, "method: %s", method);
        // if method is set, then set property
        if (strcmp(method, "set") == 0)
        {
            // get key
            cJSON *key_item = cJSON_GetObjectItem(root, "key");
            cJSON *value = cJSON_GetObjectItem(root, "value");
            if (cJSON_IsString(key_item) && value)
            {
                if (set_property(key_item->valuestring, value, msg_id) != ESP_OK) {
                    valid_command = false;
                }
            } else {
                ESP_LOGW(TAG, "Invalid set command");
                valid_command = false;
            }
            // if method is get, then get property
        }
        else if (strcmp(method, "get") == 0)
        {
            // get key
            cJSON *key_item = cJSON_GetObjectItem(root, "key");
            if (cJSON_IsString(key_item))
            {
                get_property(key_item->valuestring, msg_id);
            } else {
                ESP_LOGW(TAG, "Invalid get command");
                valid_command = false;
            }
        }else if(strcmp(method, "update") == 0){
            // update property in root`s keys
            cJSON *child = root->child;
            while(child != NULL){
                // skip method field itself
                if(strcmp(child->string, "method") != 0 &&
                   strcmp(child->string, "msg_id") != 0){
                    ESP_LOGI(TAG, "update property: %s", child->string);
                    if (set_property(child->string, child, msg_id) != ESP_OK) {
                        valid_command = false;
                    }
                }
                child = child->next;
            }
        }
        else if (strcmp(method, "ota_update") == 0)
        {
            ESP_LOGI(TAG, "Received OTA update command");
            cJSON *url_item = cJSON_GetObjectItem(root, "url");
            if (cJSON_IsString(url_item)) {
                ota_perform_update(url_item->valuestring);
            } else {
                ESP_LOGW(TAG, "OTA update command missing URL");
                valid_command = false;
            }
        }
        else{
            // do action
            device_handle_action(root);
        }
    } else {
        ESP_LOGW(TAG, "Command missing method");
        valid_command = false;
    }
    if (valid_command) {
        on_mqtt_msg_process(topic, root);
        update_last_msg_time();
    }
    cJSON_Delete(root);

    if (s_receive_mutex) {
        xSemaphoreGiveRecursive(s_receive_mutex);
    }
}

void mqtt_msg_process(char *topic, int topic_len, char *data, int data_len)
{
    device_handle_receive(topic, topic_len, data, data_len);
}

esp_err_t set_property(char *property_name, cJSON *property_value, int msg_id)
{
    esp_err_t result = ESP_OK;

    if (!property_name || !property_value) {
        ESP_LOGW(TAG, "Invalid property update");
        return ESP_ERR_INVALID_ARG;
    }
    if (!device_is_ready()) {
        ESP_LOGW(TAG, "Ignoring property update before device first-ready");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_receive_mutex) {
        xSemaphoreTakeRecursive(s_receive_mutex, portMAX_DELAY);
    }

    char *json_string = cJSON_Print(property_value);
    if (json_string) {
        ESP_LOGI(TAG, "set_property property_name: %s, property_value: %s",
                 property_name, json_string);
        free(json_string);
    }
    device_property_t *property = NULL;
    for(int i = 0; i < device_properties_num; i++){
        if(strcmp(property_name, device_properties[i]->name) == 0){
            property = device_properties[i];
            break;
        }
    }
    if(property == NULL){
        ESP_LOGE(TAG, "property_name: %s is not supported", property_name);
        result = ESP_ERR_NOT_SUPPORTED;
        goto done;
    }
    
    if (property->value_type == PROPERTY_TYPE_INT)
    {
        if (!cJSON_IsNumber(property_value)) {
            ESP_LOGW(TAG, "Property %s requires a number", property_name);
            result = ESP_ERR_INVALID_ARG;
            goto done;
        }
        property->value.int_value = property_value->valueint;
    }
    else if (property->value_type == PROPERTY_TYPE_FLOAT)
    {
        if (!cJSON_IsNumber(property_value)) {
            ESP_LOGW(TAG, "Property %s requires a number", property_name);
            result = ESP_ERR_INVALID_ARG;
            goto done;
        }
        property->value.float_value = property_value->valuedouble;
    }
    else if (property->value_type == PROPERTY_TYPE_STRING)
    {
        if (!cJSON_IsString(property_value)) {
            ESP_LOGW(TAG, "Property %s requires a string", property_name);
            result = ESP_ERR_INVALID_ARG;
            goto done;
        }
        strlcpy(property->value.string_value, property_value->valuestring,
                sizeof(property->value.string_value));
    }
    else
    {
        ESP_LOGE(TAG, "property->value_type: %d is not supported", property->value_type);
        result = ESP_ERR_NOT_SUPPORTED;
        goto done;
    }
    on_set_property(property_name, property_value, msg_id);

done:
    if (s_receive_mutex) {
        xSemaphoreGiveRecursive(s_receive_mutex);
    }
    return result;
}

void get_property(char *property_name, int msg_id)
{
    if(g_device_mode == MODE_BLE && !device_serial_debug_is_active()) return;
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
    if (!root) {
        ESP_LOGE(TAG, "Failed to create property response");
        return;
    }
    cJSON_AddStringToObject(root, "method", "update");
    if(msg_id >= 0)cJSON_AddNumberToObject(root, "msg_id", msg_id);
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
    device_publish_message(root);
}

static void device_publish_serialized(const char *json_data, bool include_ble)
{
    if (smqtt_connected && smqtt_client) {
        ESP_LOGI(TAG, "publish(mqtt): %s", json_data);
        esp_mqtt_client_publish(smqtt_client, publish_topic, json_data, 0, 1, 0);
    }

    if (device_serial_debug_is_active()) {
        esp_err_t err = device_serial_debug_send_payload(json_data, strlen(json_data));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "publish(serial) failed: %s", esp_err_to_name(err));
        }
    }

    if (include_ble && g_device_mode == MODE_BLE) {
        ESP_LOGI(TAG, "publish(ble): %s", json_data);
        device_ble_send_message(json_data);
    }
}

static void device_publish_internal(cJSON *root, bool include_ble)
{
    if (!root) {
        return;
    }

    char *json_data = cJSON_PrintUnformatted(root);
    if (json_data) {
        device_publish_serialized(json_data, include_ble);
        free(json_data);
    } else {
        ESP_LOGE(TAG, "Failed to serialize outgoing message");
    }
    cJSON_Delete(root);
}

void device_publish_message(cJSON *root)
{
    device_publish_internal(root, false);
}

void mqtt_publish(cJSON *root)
{
    device_publish_message(root);
}

void device_publish_event(cJSON *root)
{
    device_publish_internal(root, true);
}

void device_send_ble_message(const char *message)
{
    device_ble_send_message(message);
}

void device_update_property_int(const char *name, int v){
    int i = -1;
    for (int k = 0; k < device_properties_num; k++){
        if (strcmp(device_properties[k]->name, name) == 0){ i = k; break; }
    }
    if (i < 0) return;
    device_properties[i]->value.int_value = v;
    if(g_device_mode == MODE_BLE){
        device_ble_update_property(i);
    }
}

void device_update_property_float(const char *name, float v){
    int i = -1;
    for (int k = 0; k < device_properties_num; k++){
        if (strcmp(device_properties[k]->name, name) == 0){ i = k; break; }
    }
    if (i < 0) return;
    device_properties[i]->value.float_value = v;
    if(g_device_mode == MODE_BLE){
        device_ble_update_property(i);
    }
}

void device_update_property_string(const char *name, const char *v){
    int i = -1;
    for (int k = 0; k < device_properties_num; k++){
        if (strcmp(device_properties[k]->name, name) == 0){ i = k; break; }
    }
    if (i < 0) return;
    strncpy(device_properties[i]->value.string_value, v, PROPERTY_VALUE_MAX - 1);
    device_properties[i]->value.string_value[PROPERTY_VALUE_MAX - 1] = 0;
    if(g_device_mode == MODE_BLE){
        device_ble_update_property(i);
    }
}
