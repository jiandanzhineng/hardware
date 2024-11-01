#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mqtt_client.h"

#include "cJSON.h"

/*define PROPERTY_NAME_MAX as 32*/
#define PROPERTY_NAME_MAX 32
/*define PROPERTY_VALUE_MAX as 32*/
#define PROPERTY_VALUE_MAX 32

#define PROPERTY_TYPE_INT 0
#define PROPERTY_TYPE_FLOAT 1
#define PROPERTY_TYPE_STRING 2

#define true 1
#define false 0

/* 
define a struct for device property 
its properties are "readable" "writeable" "name" "value_type" "value"
"value_type" is 0 for int, 1 for float, 2 for string
*/
typedef struct device_property {
    int readable;
    int writeable;
    int busy;
    int min;
    int max;
    char name[PROPERTY_NAME_MAX];
    int value_type;
    union {
        int int_value;
        float float_value;
        char string_value[PROPERTY_VALUE_MAX];
    } value;
} device_property_t;

extern esp_mqtt_client_handle_t smqtt_client;
extern char publish_topic[32];


void on_mqtt_msg_process(char *topic, cJSON *root);
void on_set_property(char *property_name, cJSON *property_value, int msg_id);
void on_device_init(void);
void on_device_first_ready(void);
void on_action(cJSON *root);