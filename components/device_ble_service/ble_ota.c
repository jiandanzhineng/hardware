#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "ble_ota.h"

#define TAG "BLE_OTA"

#define CMD_START           0x01
#define CMD_END             0x02
#define CMD_ABORT           0x03

#define STATUS_ACK          0x00
#define STATUS_SUCCESS      0x01
#define STATUS_CRC_ERROR    0x02
#define STATUS_FAIL         0x03

static esp_gatt_if_t ota_gatts_if = ESP_GATT_IF_NONE;
static uint16_t ota_conn_id = 0xffff;
static uint16_t ota_notify_handle = 0;

static esp_ota_handle_t update_handle = 0;
static const esp_partition_t *update_partition = NULL;
static bool ota_running = false;
static uint32_t current_crc = 0;
static size_t total_received = 0;
static size_t image_size = 0;

void ble_ota_set_params(esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t handle_notify) {
    ota_gatts_if = gatts_if;
    ota_conn_id = conn_id;
    ota_notify_handle = handle_notify;
    if (conn_id == 0xffff) {
        ota_running = false; // Reset on disconnect
    }
}

static void notify_status(uint8_t status) {
    if (ota_conn_id != 0xffff && ota_gatts_if != ESP_GATT_IF_NONE && ota_notify_handle != 0) {
        esp_ble_gatts_send_indicate(ota_gatts_if, ota_conn_id, ota_notify_handle, 1, &status, false);
    }
}

void ble_ota_handle_command(const uint8_t *data, uint16_t len) {
    if (len < 1) return;
    uint8_t cmd = data[0];

    if (cmd == CMD_START) {
        if (len < 5) return;
        image_size = (data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24));
        ESP_LOGI(TAG, "OTA Start, size: %d", image_size);

        update_partition = esp_ota_get_next_update_partition(NULL);
        if (update_partition == NULL) {
            ESP_LOGE(TAG, "No OTA partition found");
            notify_status(STATUS_FAIL);
            return;
        }

        if (esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle) != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed");
            notify_status(STATUS_FAIL);
            return;
        }

        ota_running = true;
        current_crc = 0;
        total_received = 0;
        notify_status(STATUS_ACK);

    } else if (cmd == CMD_END) {
        if (!ota_running) return;
        if (len < 5) {
            notify_status(STATUS_FAIL);
            return;
        }

        uint32_t received_crc = (data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24));
        ESP_LOGI(TAG, "OTA End. Calc CRC: 0x%08x, Recv CRC: 0x%08x", (unsigned int)current_crc, (unsigned int)received_crc);

        if (current_crc != received_crc) {
            ESP_LOGE(TAG, "CRC Mismatch!");
            notify_status(STATUS_CRC_ERROR);
            esp_ota_end(update_handle);
            ota_running = false;
            return;
        }

        if (esp_ota_end(update_handle) != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed");
            notify_status(STATUS_FAIL);
            return;
        }

        if (esp_ota_set_boot_partition(update_partition) != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed");
            notify_status(STATUS_FAIL);
            return;
        }

        ESP_LOGI(TAG, "OTA Success. Rebooting...");
        notify_status(STATUS_SUCCESS);
        ota_running = false;
        
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();

    } else if (cmd == CMD_ABORT) {
        ESP_LOGI(TAG, "OTA Abort");
        if (ota_running) {
            esp_ota_end(update_handle);
            ota_running = false;
        }
    }
}

void ble_ota_handle_data(const uint8_t *data, uint16_t len) {
    if (!ota_running) {
        ESP_LOGW(TAG, "OTA Data received but ota_running is false. Ignored.");
        return;
    }

    if (esp_ota_write(update_handle, data, len) != ESP_OK) {
        ESP_LOGE(TAG, "OTA write failed");
        ota_running = false;
        notify_status(STATUS_FAIL);
        return;
    }

    current_crc = esp_rom_crc32_le(current_crc, data, len);
    total_received += len;
    
    // Optional: Log progress every 1KB or so to avoid flooding logs
    if (total_received % (1024) < len) {
        ESP_LOGI(TAG, "OTA Progress: %d / %d", total_received, image_size);
    }
}
