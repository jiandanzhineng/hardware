idf_component_register(SRCS "blufi_example_main.c"
                            "blufi_security.c"
                            "blufi_init.c"
                            "device_ble_service.c"
                    INCLUDE_DIRS "."
                    REQUIRES  "esp_wifi" "nvs_flash" "bt" "json" "smqtt" "mqtt" "esp_netif" "base_device" "driver"
                    )
