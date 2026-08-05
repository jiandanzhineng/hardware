#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

esp_err_t device_serial_debug_init(void);
bool device_serial_debug_is_active(void);
esp_err_t device_serial_debug_send_payload(const char *payload, size_t len);
