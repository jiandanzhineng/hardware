#include "device_identity.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"

#define DEVICE_ID_LENGTH 12

esp_err_t device_identity_write_json(char *buffer, size_t buffer_size, size_t *written)
{
    if (!buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac[6];
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        return err;
    }

    char device_id[DEVICE_ID_LENGTH + 1];
    int device_id_len = snprintf(device_id, sizeof(device_id),
                                 "%02x%02x%02x%02x%02x%02x",
                                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (device_id_len != DEVICE_ID_LENGTH) {
        return ESP_FAIL;
    }

    const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    if (!app_desc) {
        return ESP_ERR_INVALID_STATE;
    }

    char firmware_version[sizeof(app_desc->version) + 1];
    memcpy(firmware_version, app_desc->version, sizeof(app_desc->version));
    firmware_version[sizeof(app_desc->version)] = '\0';

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON *device_id_item = cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON *firmware_version_item =
        cJSON_AddStringToObject(root, "firmware_version", firmware_version);
    if (!device_id_item || !firmware_version_item) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    if (!cJSON_PrintPreallocated(root, buffer, buffer_size, false)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }
    cJSON_Delete(root);

    if (written) {
        *written = strlen(buffer);
    }
    return ESP_OK;
}
