idf_component_register(SRCS "smqtt.c"
                        INCLUDE_DIRS "include"
                        REQUIRES "driver" "mqtt" "esp_netif" "protocol_examples_common" "base_device")

