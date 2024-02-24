#include "cJSON.h"

void on_mqtt_msg_process(char *topic, cJSON *root);
void on_set_property(char *property_name, cJSON *property_value, int msg_id);
void on_device_init(void);
void on_device_first_ready(void);
void on_action(cJSON *root);