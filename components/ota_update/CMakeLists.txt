idf_component_register(SRCS "ota_update.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_https_ota app_update esp_http_client json smqtt esp_timer)
