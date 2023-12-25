#include "cJSON.h"

void on_mqtt_msg_process(char *topic, int topic_len, char *data, int data_len);
void on_set_property(char *property_name, cJSON *property_value, int msg_id);
void on_device_init(void);
void on_device_first_ready(void);
void on_action(cJSON *root);