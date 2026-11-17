idf_component_register(SRCS "sk_main.c" "lock_control.c" "test_policy.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver esp_adc esp_timer esp_wifi esp_event esp_netif
                  nvs_flash app_update esp_app_format spi_flash esp_hw_support)
