set(srcs "base_device.c" )

if(CONFIG_DEVICE_DIANJI)
    list(APPEND srcs "dianji.c")
endif()

if(CONFIG_DEVICE_TD01)
    list(APPEND srcs "TD01.c")
endif()

idf_component_register(SRCS "${srcs}"
                        INCLUDE_DIRS "include"
                        REQUIRES "mqtt" "json" "button" "Battery")







