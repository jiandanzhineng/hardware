
#include "sdkconfig.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

#define DEVICE_TJS 1
#define DEVICE_TD01 2
#define DEVICE_DIANJI 3
#define DEVICE_QTZ 4
#define DEVICE_ZIDONGSUO 5
#define DEVICE_PJ01 6
#define DEVICE_QIYA 7
#define DEVICE_DZC01 8
#define DEVICE_CUNZHI01 9


#ifdef CONFIG_DEVICE_TD01
    #define DEVICE_TYPE_INDEX DEVICE_TD01
    #define DEVICE_TYPE_NAME "TD01"
    #define CONNECTED_LED GPIO_NUM_10
#endif

#ifdef CONFIG_DEVICE_TJS
    #define DEVICE_TYPE_INDEX DEVICE_TJS
    #define DEVICE_TYPE_NAME "TJS"
#endif

#ifdef CONFIG_DEVICE_DIANJI
    #define DEVICE_TYPE_INDEX DEVICE_DIANJI
    #define DEVICE_TYPE_NAME "DIANJI"
    #define CONNECTED_LED GPIO_NUM_7
#endif

#ifdef CONFIG_DEVICE_QTZ
    #define DEVICE_TYPE_INDEX DEVICE_QTZ
    #define DEVICE_TYPE_NAME "QTZ"
    #define CONNECTED_LED GPIO_NUM_10
#endif

#ifdef CONFIG_DEVICE_ZIDONGSUO
    #define DEVICE_TYPE_INDEX DEVICE_ZIDONGSUO
    #define DEVICE_TYPE_NAME "ZIDONGSUO"
    #define CONNECTED_LED GPIO_NUM_10
    #define CONNECTED_LED_HIGH_ENABLE
#endif

#ifdef CONFIG_DEVICE_PJ01
    #define DEVICE_TYPE_INDEX DEVICE_PJ01
    #define DEVICE_TYPE_NAME "PJ01"
    #define CONNECTED_LED GPIO_NUM_10
    #define CONNECTED_LED_HIGH_ENABLE
#endif

#ifdef CONFIG_DEVICE_QIYA
    #define DEVICE_TYPE_INDEX DEVICE_QIYA
    #define DEVICE_TYPE_NAME "QIYA"
    #define CONNECTED_LED GPIO_NUM_10
#endif

#ifdef CONFIG_DEVICE_DZC01
    #define DEVICE_TYPE_INDEX DEVICE_DZC01
    #define DEVICE_TYPE_NAME "DZC01"
    #define CONNECTED_LED GPIO_NUM_4
#endif // 电子秤01

#ifdef CONFIG_DEVICE_CUNZHI01
    #define DEVICE_TYPE_INDEX DEVICE_CUNZHI01
    #define DEVICE_TYPE_NAME "CUNZHI01"
    #define CONNECTED_LED GPIO_NUM_10
#endif


#ifdef CONNECTED_LED_HIGH_ENABLE
    #define CONNECTED_CLOSED_LED_LEVEL 0
#else
    #define CONNECTED_CLOSED_LED_LEVEL 1
#endif


#ifndef DEVICE_TYPE_INDEX
    #error "Please select a device type in menuconfig."
#endif

// device_ble_service 为每个属性建 4 个 attribute（characteristic 声明、value、
// CCCD、User Description），加上服务本身的 10 个固定 attribute。CCCD 只有
// readable 属性才有，所以这是最坏情况的上界。
#define DEVICE_BLE_BASE_ATTR_COUNT   10
#define DEVICE_BLE_ATTR_PER_PROPERTY 4
#define DEVICE_BLE_ATTR_COUNT_MAX(n) \
    (DEVICE_BLE_BASE_ATTR_COUNT + (n) * DEVICE_BLE_ATTR_PER_PROPERTY)

extern int device_properties_num;

// 定义 device_properties_num，并在编译期确认 GATT 属性表不会超过
// CONFIG_BT_GATT_MAX_SR_ATTRIBUTES —— 超了 esp_ble_gatts_create_attr_tab()
// 只会返回 ESP_ERR_INVALID_ARG，服务永远起不来，设备能被扫描到却连不上。
#define DEVICE_PROPERTIES_NUM_DEFINE(arr)                                      \
    int device_properties_num = sizeof(arr) / sizeof((arr)[0]);                \
    _Static_assert(                                                            \
        DEVICE_BLE_ATTR_COUNT_MAX(sizeof(arr) / sizeof((arr)[0])) <=           \
            CONFIG_BT_GATT_MAX_SR_ATTRIBUTES,                                  \
        "GATT attribute table exceeds CONFIG_BT_GATT_MAX_SR_ATTRIBUTES; "      \
        "raise it in sdkconfig.defaults or remove a property")

#pragma message "DEVICE_TYPE_NAME: " DEVICE_TYPE_NAME
    


void mqtt_msg_process(char *topic, int topic_len, char *data, int data_len);
void device_handle_receive(char *topic, int topic_len, char *data, int data_len);
void device_handle_action(cJSON *root);
esp_err_t set_property(char *property_name, cJSON *property_value, int msg_id);
void get_property(char *property_name, int msg_id);
void device_init(void);
esp_err_t device_first_ready(void);
bool device_is_ready(void);
void device_report_all_properties(void);
// Serializes once, publishes to MQTT and serial when available, and releases root.
void device_publish_message(cJSON *root);
void mqtt_publish(cJSON *root);
// 统一的事件/动作发送入口：发送到可用的 MQTT、串口和 BLE 0xFF01 Message 特征。
// 仅用于 action/event 类（非属性）消息；属性值在 BLE 下走 GATT 属性特征。
// 负责释放传入的 root。
void device_publish_event(cJSON *root);
void device_send_ble_message(const char *message);
void update_last_msg_time(void);
void device_update_property_int(const char *name, int v);
void device_update_property_float(const char *name, float v);
void device_update_property_string(const char *name, const char *v);
