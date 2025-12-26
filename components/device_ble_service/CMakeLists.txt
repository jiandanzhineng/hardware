idf_component_register(
  SRCS "device_ble_service.c" "ble_ota.c"
  INCLUDE_DIRS "include"
  REQUIRES "base_device" "esp_wifi" "bt" "json" "app_update"
)
