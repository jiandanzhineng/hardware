
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
#endif // 电子秤01

#ifdef CONNECTED_LED_HIGH_ENABLE
    #define CONNECTED_CLOSED_LED_LEVEL 0
#else
    #define CONNECTED_CLOSED_LED_LEVEL 1
#endif


#ifndef DEVICE_TYPE_INDEX  
    #error "Please select a device type in menuconfig."  
#endif

#pragma message "DEVICE_TYPE_NAME: " DEVICE_TYPE_NAME
    


void mqtt_msg_process(char *topic, int topic_len, char *data, int data_len);
void set_property(char *property_name, cJSON *property_value, int msg_id);
void get_property(char *property_name, int msg_id);
void device_init(void);
void device_first_ready(void);
void mqtt_publish(cJSON *root);
static void report_all_properties(void);
static void heartbeat_task(void);
static void sleep_check_task(void);