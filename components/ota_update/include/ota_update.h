#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Manually trigger an OTA update from a specific URL
 * 
 * This function spawns a task to download and install the firmware from the given URL.
 * 
 * @param url The URL of the firmware (.bin) or version file (.json) - Implementation decides.
 *            Based on user request "device gets upgrade file according to url link", 
 *            we treat this as the firmware URL.
 */
void ota_perform_update(const char *url);

/**
 * @brief Mark the current running app as valid to cancel rollback
 * 
 * This should be called after the device has successfully booted and verified its functionality
 * (e.g. connected to Wi-Fi/MQTT).
 */
void ota_mark_app_valid(void);

#ifdef __cplusplus
}
#endif
