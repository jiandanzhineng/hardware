#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "device_ble_service.h"
#include "esp_wifi.h"

#define TAG "DEVICE_BLE"

#define SERVICE_UUID        0x00FF
#define CHAR_UUID           0xFF01
#define MODE_CHAR_UUID      0xFF02
#define APP_ID              1

enum {
    IDX_SVC,
    IDX_CHAR_DEC,
    IDX_CHAR_VAL,
    IDX_MODE_CHAR_DEC,
    IDX_MODE_CHAR_VAL,
    IDX_NB,
};

static uint16_t handle_table[IDX_NB];

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint16_t service_uuid = SERVICE_UUID;
static const uint16_t char_uuid = CHAR_UUID;
static const uint8_t char_value[1] = {0x00};
static const uint16_t mode_char_uuid = MODE_CHAR_UUID;
static uint8_t mode_value[1] = {0x00};

static const esp_gatts_attr_db_t gatt_db[IDX_NB] = {
    // Service Declaration
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
         sizeof(uint16_t), sizeof(service_uuid), (uint8_t *)&service_uuid}
    },

    // Characteristic Declaration
    [IDX_CHAR_DEC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(char_prop_write), (uint8_t *)&char_prop_write}
    },

    // Characteristic Value
    [IDX_CHAR_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&char_uuid, ESP_GATT_PERM_WRITE,
         sizeof(char_value), sizeof(char_value), (uint8_t *)char_value}
    },

    [IDX_MODE_CHAR_DEC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(char_prop_write), (uint8_t *)&char_prop_write}
    },

    [IDX_MODE_CHAR_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&mode_char_uuid, ESP_GATT_PERM_WRITE,
         sizeof(mode_value), sizeof(mode_value), (uint8_t *)mode_value}
    },
};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            if (param->reg.app_id == APP_ID) {
                esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, IDX_NB, 0);
            }
            break;
            
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status == ESP_GATT_OK && param->add_attr_tab.num_handle == IDX_NB) {
                memcpy(handle_table, param->add_attr_tab.handles, sizeof(handle_table));
                esp_ble_gatts_start_service(handle_table[IDX_SVC]);
            } else {
                ESP_LOGE(TAG, "Create attribute table failed, error code = %x", param->add_attr_tab.status);
            }
            break;
            
        case ESP_GATTS_WRITE_EVT:
            ESP_LOGI(TAG, "WRITE_EVT handle=0x%04x len=%d", param->write.handle, param->write.len);
            if (handle_table[IDX_CHAR_VAL] == param->write.handle) {
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
            } else if (handle_table[IDX_MODE_CHAR_VAL] == param->write.handle) {
                if (param->write.len > 0) {
                    uint8_t v = param->write.value[0];
                    mode_value[0] = v;
                    if (v == 0x01) {
                        esp_wifi_stop();
                        ESP_LOGI(TAG, "Mode: BLE");
                    } else if (v == 0x00) {
                        esp_wifi_start();
                        ESP_LOGI(TAG, "Mode: WiFi");
                    }
                }
            }
            break;
            
        default:
            break;
    }
}

void device_ble_service_init(void) {
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_profile_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(APP_ID));
}
