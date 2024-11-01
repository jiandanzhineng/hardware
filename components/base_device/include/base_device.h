
#include "mqtt_client.h"
#include "cJSON.h"
#include "driver/ledc.h"

#define DEVICE_TJS 1
#define DEVICE_TD01 2
#define DEVICE_DIANJI 3
#define DEVICE_QTZ 4


#ifdef CONFIG_DEVICE_TD01
    #define DEVICE_TYPE_INDEX DEVICE_TD01
    #define DEVICE_TYPE_NAME "TD01"
#endif

#ifdef CONFIG_DEVICE_TJS
    #define DEVICE_TYPE_INDEX DEVICE_TJS
    #define DEVICE_TYPE_NAME "TJS"
#endif

#ifdef CONFIG_DEVICE_DIANJI
    #define DEVICE_TYPE_INDEX DEVICE_DIANJI
    #define DEVICE_TYPE_NAME "DIANJI"
#endif

#ifdef CONFIG_DEVICE_QTZ
    #define DEVICE_TYPE_INDEX DEVICE_QTZ
    #define DEVICE_TYPE_NAME "QTZ"
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