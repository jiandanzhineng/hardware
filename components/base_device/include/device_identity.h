#pragma once

#include <stddef.h>

#include "esp_err.h"

#define DEVICE_IDENTITY_JSON_MAX_LEN 256

// Writes a compact JSON object containing device_id and firmware_version.
esp_err_t device_identity_write_json(char *buffer, size_t buffer_size, size_t *written);
