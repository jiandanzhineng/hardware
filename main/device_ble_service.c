#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "device_ble_service.h"
#include "device_common.h"
#include "base_device.h"
#include "esp_wifi.h"

#define TAG "DEVICE_BLE"

#define SERVICE_UUID        0x00FF
#define CHAR_UUID           0xFF01
#define MODE_CHAR_UUID      0xFF02
#define APP_ID              1

static esp_gatts_attr_db_t *gatt_db = NULL;
static uint16_t *handle_table = NULL;
static int total_attr_count = 0;
static int base_attr_count = 5;
static esp_gatt_if_t s_gatts_if = 0;
static int conn_id_last = -1;
extern device_property_t *device_properties[];
extern int device_properties_num;
static int *prop_value_index = NULL;
static int *prop_cccd_index = NULL;
static uint8_t **prop_value_buf = NULL;
static uint16_t *prop_value_len = NULL;
static uint16_t *prop_value_maxlen = NULL;
static uint8_t **prop_cccd_buf = NULL;
static uint8_t *prop_notify_enabled = NULL;
static uint16_t *prop_uuid16_arr = NULL;
static uint8_t *prop_char_props_arr = NULL;

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint16_t service_uuid = SERVICE_UUID;
static const uint16_t char_uuid = CHAR_UUID;
static const uint8_t char_value[1] = {0x00};
static const uint16_t mode_char_uuid = MODE_CHAR_UUID;
static uint8_t mode_value[1] = {0x00};

static void build_gatt_table(void) {
    int include_count = 0;
    for (int i = 0; i < device_properties_num; i++) {
        device_property_t *p = device_properties[i];
        if (p->readable || p->writeable) include_count++;
    }
    int props = include_count;
    int extra_cccd = 0;
    for (int i = 0; i < device_properties_num; i++) {
        device_property_t *p = device_properties[i];
        if (p->readable || p->writeable) {
            if (p->readable) extra_cccd++;
        }
    }
    total_attr_count = base_attr_count + props * 2 + extra_cccd + props; // Add props count for User Description
    gatt_db = (esp_gatts_attr_db_t *)malloc(sizeof(esp_gatts_attr_db_t) * total_attr_count);
    handle_table = (uint16_t *)malloc(sizeof(uint16_t) * total_attr_count);
    prop_value_index = (int *)malloc(sizeof(int) * device_properties_num);
    prop_cccd_index = (int *)malloc(sizeof(int) * device_properties_num);
    prop_value_buf = (uint8_t **)malloc(sizeof(uint8_t *) * device_properties_num);
    prop_cccd_buf = (uint8_t **)malloc(sizeof(uint8_t *) * device_properties_num);
    prop_value_len = (uint16_t *)malloc(sizeof(uint16_t) * device_properties_num);
    prop_value_maxlen = (uint16_t *)malloc(sizeof(uint16_t) * device_properties_num);
    prop_notify_enabled = (uint8_t *)malloc(sizeof(uint8_t) * device_properties_num);
    prop_uuid16_arr = (uint16_t *)malloc(sizeof(uint16_t) * device_properties_num);
    prop_char_props_arr = (uint8_t *)malloc(sizeof(uint8_t) * device_properties_num);
    for (int i = 0; i < device_properties_num; i++) {
        prop_value_index[i] = -1;
        prop_cccd_index[i] = -1;
        prop_value_buf[i] = NULL;
        prop_cccd_buf[i] = NULL;
        prop_value_len[i] = 0;
        prop_value_maxlen[i] = 0;
        prop_notify_enabled[i] = 0;
        prop_uuid16_arr[i] = (uint16_t)(0xFF10 + i);
        prop_char_props_arr[i] = 0;
    }

    int idx = 0;
    gatt_db[idx].attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
    gatt_db[idx].att_desc.uuid_length = ESP_UUID_LEN_16;
    gatt_db[idx].att_desc.uuid_p = (uint8_t *)&primary_service_uuid;
    gatt_db[idx].att_desc.perm = ESP_GATT_PERM_READ;
    gatt_db[idx].att_desc.max_length = sizeof(uint16_t);
    gatt_db[idx].att_desc.length = sizeof(service_uuid);
    gatt_db[idx].att_desc.value = (uint8_t *)&service_uuid;
    idx++;

    gatt_db[idx].attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
    gatt_db[idx].att_desc.uuid_length = ESP_UUID_LEN_16;
    gatt_db[idx].att_desc.uuid_p = (uint8_t *)&character_declaration_uuid;
    gatt_db[idx].att_desc.perm = ESP_GATT_PERM_READ;
    gatt_db[idx].att_desc.max_length = sizeof(uint8_t);
    gatt_db[idx].att_desc.length = sizeof(char_prop_write);
    gatt_db[idx].att_desc.value = (uint8_t *)&char_prop_write;
    idx++;

    gatt_db[idx].attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
    gatt_db[idx].att_desc.uuid_length = ESP_UUID_LEN_16;
    gatt_db[idx].att_desc.uuid_p = (uint8_t *)&char_uuid;
    gatt_db[idx].att_desc.perm = ESP_GATT_PERM_WRITE;
    gatt_db[idx].att_desc.max_length = sizeof(char_value);
    gatt_db[idx].att_desc.length = sizeof(char_value);
    gatt_db[idx].att_desc.value = (uint8_t *)char_value;
    idx++;

    gatt_db[idx].attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
    gatt_db[idx].att_desc.uuid_length = ESP_UUID_LEN_16;
    gatt_db[idx].att_desc.uuid_p = (uint8_t *)&character_declaration_uuid;
    gatt_db[idx].att_desc.perm = ESP_GATT_PERM_READ;
    gatt_db[idx].att_desc.max_length = sizeof(uint8_t);
    gatt_db[idx].att_desc.length = sizeof(char_prop_write);
    gatt_db[idx].att_desc.value = (uint8_t *)&char_prop_write;
    idx++;

    gatt_db[idx].attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
    gatt_db[idx].att_desc.uuid_length = ESP_UUID_LEN_16;
    gatt_db[idx].att_desc.uuid_p = (uint8_t *)&mode_char_uuid;
    gatt_db[idx].att_desc.perm = ESP_GATT_PERM_WRITE;
    gatt_db[idx].att_desc.max_length = sizeof(mode_value);
    gatt_db[idx].att_desc.length = sizeof(mode_value);
    gatt_db[idx].att_desc.value = (uint8_t *)mode_value;
    idx++;

    for (int i = 0; i < device_properties_num; i++) {
        device_property_t *p = device_properties[i];
        if (!(p->readable || p->writeable)) continue;
        uint8_t props = 0;
        if (p->readable) props |= ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        if (p->writeable) props |= ESP_GATT_CHAR_PROP_BIT_WRITE;
        prop_char_props_arr[i] = props;

        gatt_db[idx].attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
        gatt_db[idx].att_desc.uuid_length = ESP_UUID_LEN_16;
        gatt_db[idx].att_desc.uuid_p = (uint8_t *)&character_declaration_uuid;
        gatt_db[idx].att_desc.perm = ESP_GATT_PERM_READ;
        gatt_db[idx].att_desc.max_length = sizeof(uint8_t);
        gatt_db[idx].att_desc.length = sizeof(uint8_t);
        gatt_db[idx].att_desc.value = &prop_char_props_arr[i];
        idx++;

        uint16_t perm = 0;
        if (p->readable) perm |= ESP_GATT_PERM_READ;
        if (p->writeable) perm |= ESP_GATT_PERM_WRITE;

        uint16_t maxlen = 0;
        uint16_t curlen = 0;
        if (p->value_type == PROPERTY_TYPE_INT) {
            maxlen = 4;
            curlen = 4;
            prop_value_buf[i] = (uint8_t *)malloc(maxlen);
            int v = p->value.int_value;
            prop_value_buf[i][0] = (uint8_t)(v & 0xFF);
            prop_value_buf[i][1] = (uint8_t)((v >> 8) & 0xFF);
            prop_value_buf[i][2] = (uint8_t)((v >> 16) & 0xFF);
            prop_value_buf[i][3] = (uint8_t)((v >> 24) & 0xFF);
        } else if (p->value_type == PROPERTY_TYPE_FLOAT) {
            maxlen = 4;
            curlen = 4;
            prop_value_buf[i] = (uint8_t *)malloc(maxlen);
            float f = p->value.float_value;
            memcpy(prop_value_buf[i], &f, 4);
        } else {
            maxlen = PROPERTY_VALUE_MAX;
            size_t sl = strnlen(p->value.string_value, PROPERTY_VALUE_MAX);
            curlen = (uint16_t)sl;
            prop_value_buf[i] = (uint8_t *)malloc(maxlen);
            memcpy(prop_value_buf[i], p->value.string_value, sl);
        }
        prop_value_len[i] = curlen;
        prop_value_maxlen[i] = maxlen;

        gatt_db[idx].attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
        gatt_db[idx].att_desc.uuid_length = ESP_UUID_LEN_16;
        gatt_db[idx].att_desc.uuid_p = (uint8_t *)&prop_uuid16_arr[i];
        gatt_db[idx].att_desc.perm = perm;
        gatt_db[idx].att_desc.max_length = maxlen;
        gatt_db[idx].att_desc.length = curlen;
        gatt_db[idx].att_desc.value = prop_value_buf[i];
        prop_value_index[i] = idx;
        idx++;

        if (p->readable) {
            static const uint16_t cccd_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            prop_cccd_buf[i] = (uint8_t *)malloc(2);
            prop_cccd_buf[i][0] = 0;
            prop_cccd_buf[i][1] = 0;
            gatt_db[idx].attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
            gatt_db[idx].att_desc.uuid_length = ESP_UUID_LEN_16;
            gatt_db[idx].att_desc.uuid_p = (uint8_t *)&cccd_uuid;
            gatt_db[idx].att_desc.perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE;
            gatt_db[idx].att_desc.max_length = 2;
            gatt_db[idx].att_desc.length = 2;
            gatt_db[idx].att_desc.value = prop_cccd_buf[i];
            prop_cccd_index[i] = idx;
            idx++;
        }

        // Add User Description Descriptor (0x2901)
        static const uint16_t user_desc_uuid = ESP_GATT_UUID_CHAR_DESCRIPTION;
        gatt_db[idx].attr_control.auto_rsp = ESP_GATT_AUTO_RSP;
        gatt_db[idx].att_desc.uuid_length = ESP_UUID_LEN_16;
        gatt_db[idx].att_desc.uuid_p = (uint8_t *)&user_desc_uuid;
        gatt_db[idx].att_desc.perm = ESP_GATT_PERM_READ;
        gatt_db[idx].att_desc.max_length = PROPERTY_VALUE_MAX;
        gatt_db[idx].att_desc.length = strlen(p->name);
        gatt_db[idx].att_desc.value = (uint8_t *)p->name;
        idx++;
    }
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    ESP_LOGI(TAG, "gatts_profile_event_handler, event = %d", event);
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(TAG, "ESP_GATTS_REG_EVT, app_id = %d", param->reg.app_id);
            s_gatts_if = gatts_if;
            if (param->reg.app_id == APP_ID) {
                build_gatt_table();
                esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, total_attr_count, 0);
            }
            break;
            
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            ESP_LOGI(TAG, "ESP_GATTS_CREAT_ATTR_TAB_EVT, status = %d, num_handle = %d", param->add_attr_tab.status, param->add_attr_tab.num_handle);
            if (param->add_attr_tab.status == ESP_GATT_OK && param->add_attr_tab.num_handle == total_attr_count) {
                memcpy(handle_table, param->add_attr_tab.handles, sizeof(uint16_t) * total_attr_count);
                esp_ble_gatts_start_service(handle_table[0]);
            } else {
                ESP_LOGE(TAG, "Create attribute table failed, error code = %x", param->add_attr_tab.status);
            }
            break;
            
        case ESP_GATTS_WRITE_EVT:
            ESP_LOGI(TAG, "ESP_GATTS_WRITE_EVT, conn_id = %d, handle = %d, len = %d", param->write.conn_id, param->write.handle, param->write.len);
            conn_id_last = param->write.conn_id;
            if (handle_table[2] == param->write.handle) {
                ESP_LOGI(TAG, "Received %d bytes:", param->write.len);
                esp_log_buffer_hex(TAG, param->write.value, param->write.len);
                
                // Try to print as string
                char *str = malloc(param->write.len + 1);
                if (str) {
                    memcpy(str, param->write.value, param->write.len);
                    str[param->write.len] = 0;
                    ESP_LOGI(TAG, "String: %s", str);
                    free(str);
                }
            } else if (handle_table[4] == param->write.handle) {
                if (param->write.len > 0) {
                    uint8_t v = param->write.value[0];
                    mode_value[0] = v;
                    if (v == 0x01) {
                        g_device_mode = MODE_BLE;
                        esp_wifi_stop();
                        ESP_LOGI(TAG, "Mode: BLE");
                        device_first_ready();
                    } else if (v == 0x00) {
                        g_device_mode = MODE_WIFI;
                        esp_wifi_start();
                        ESP_LOGI(TAG, "Mode: WiFi");
                    }
                }
            } else {
                for (int i = 0; i < device_properties_num; i++) {
                    if (prop_value_index[i] >= 0 && handle_table[prop_value_index[i]] == param->write.handle) {
                        device_property_t *p = device_properties[i];
                        if (p->value_type == PROPERTY_TYPE_INT) {
                            int v = 0;
                            for (int b = 0; b < param->write.len && b < 4; b++) {
                                v |= ((int)param->write.value[b]) << (8 * b);
                            }
                            esp_log_buffer_hex(TAG, param->write.value, param->write.len);
                            ESP_LOGI(TAG, "Set %s to %d", p->name, v);
                            p->value.int_value = v;
                            prop_value_buf[i][0] = (uint8_t)(v & 0xFF);
                            prop_value_buf[i][1] = (uint8_t)((v >> 8) & 0xFF);
                            prop_value_buf[i][2] = (uint8_t)((v >> 16) & 0xFF);
                            prop_value_buf[i][3] = (uint8_t)((v >> 24) & 0xFF);
                            prop_value_len[i] = 4;
                            cJSON *val = cJSON_CreateNumber(v);
                            on_set_property(p->name, val, 0);
                            cJSON_Delete(val);
                        } else if (p->value_type == PROPERTY_TYPE_FLOAT) {
                            if (param->write.len >= 4) {
                                int mantissa = (int)(param->write.value[0] | (param->write.value[1] << 8) | (param->write.value[2] << 16));
                                if (mantissa & 0x800000) mantissa |= ~0xFFFFFF;
                                int exponent = (param->write.value[3] >= 128) ? (param->write.value[3] - 256) : param->write.value[3];
                                float f = (float)mantissa;
                                while (exponent > 0) { f *= 10.0f; exponent--; }
                                while (exponent < 0) { f /= 10.0f; exponent++; }
                                esp_log_buffer_hex(TAG, param->write.value, 4);
                                ESP_LOGI(TAG, "Set %s to %f", p->name, f);
                                p->value.float_value = f;
                                memcpy(prop_value_buf[i], &f, 4);
                                prop_value_len[i] = 4;
                                cJSON *val = cJSON_CreateNumber(f);
                                on_set_property(p->name, val, 0);
                                cJSON_Delete(val);
                            }
                        } else {
                            uint16_t l = param->write.len;
                            if (l > PROPERTY_VALUE_MAX) l = PROPERTY_VALUE_MAX;
                            memcpy(p->value.string_value, param->write.value, l);
                            p->value.string_value[l < PROPERTY_VALUE_MAX ? l : PROPERTY_VALUE_MAX - 1] = 0;
                            memcpy(prop_value_buf[i], p->value.string_value, l);
                            prop_value_len[i] = l;
                            cJSON *val = cJSON_CreateString(p->value.string_value);
                            on_set_property(p->name, val, 0);
                            cJSON_Delete(val);
                        }
                        if (prop_notify_enabled[i]) {
                            esp_ble_gatts_send_indicate(s_gatts_if, param->write.conn_id, handle_table[prop_value_index[i]], prop_value_len[i], prop_value_buf[i], false);
                        }
                        break;
                    } else if (prop_cccd_index[i] >= 0 && handle_table[prop_cccd_index[i]] == param->write.handle) {
                        if (param->write.len >= 2) {
                            uint16_t cfg = param->write.value[0] | (param->write.value[1] << 8);
                            prop_cccd_buf[i][0] = param->write.value[0];
                            prop_cccd_buf[i][1] = param->write.value[1];
                            prop_notify_enabled[i] = (cfg == 0x0001);
                        }
                        break;
                    }
                }
            }
            update_last_msg_time();
            break;
            
        default:
            ESP_LOGI(TAG, "default, event = %d", event);
            break;
    }
}

void device_ble_service_init(void) {
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_profile_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(APP_ID));
}

void device_ble_update_property(int i){
    if (i < 0 || i >= device_properties_num) return;
    device_property_t *p = device_properties[i];
    if (p->value_type == PROPERTY_TYPE_INT){
        int v = p->value.int_value;
        prop_value_buf[i][0] = (uint8_t)(v & 0xFF);
        prop_value_buf[i][1] = (uint8_t)((v >> 8) & 0xFF);
        prop_value_buf[i][2] = (uint8_t)((v >> 16) & 0xFF);
        prop_value_buf[i][3] = (uint8_t)((v >> 24) & 0xFF);
        prop_value_len[i] = 4;
    } else if (p->value_type == PROPERTY_TYPE_FLOAT){
        float f = p->value.float_value;
        memcpy(prop_value_buf[i], &f, 4);
        prop_value_len[i] = 4;
    } else {
        uint16_t l = strnlen(p->value.string_value, PROPERTY_VALUE_MAX);
        memcpy(prop_value_buf[i], p->value.string_value, l);
        prop_value_len[i] = l;
    }
    if (prop_value_index[i] >= 0){
        esp_ble_gatts_set_attr_value(handle_table[prop_value_index[i]], prop_value_len[i], prop_value_buf[i]);
        if (prop_notify_enabled[i] && conn_id_last >= 0){
            esp_ble_gatts_send_indicate(s_gatts_if, conn_id_last, handle_table[prop_value_index[i]], prop_value_len[i], prop_value_buf[i], false);
        }
    }
}
