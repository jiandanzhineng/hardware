#pragma once

#include "esp_err.h"
#include "esp_gatts_api.h"

// 设置 OTA 所需的 GATT 参数
void ble_ota_set_params(esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t handle_notify);

// 处理 OTA 命令 (UUID: 0x8011)
void ble_ota_handle_command(const uint8_t *data, uint16_t len);

// 处理 OTA 数据 (UUID: 0x8012)
void ble_ota_handle_data(const uint8_t *data, uint16_t len);
