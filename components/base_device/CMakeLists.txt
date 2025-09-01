set(srcs "base_device.c" )

if(CONFIG_DEVICE_DIANJI)
    list(APPEND srcs "dianji.c")
endif()

if(CONFIG_DEVICE_TD01)
    list(APPEND srcs "TD01.c")
endif()

if(CONFIG_DEVICE_QTZ)
    list(APPEND srcs "qtz.c")
endif()

if(CONFIG_DEVICE_ZIDONGSUO)
    list(APPEND srcs "zidongsuo.c")
endif()

if(CONFIG_DEVICE_PJ01)
    list(APPEND srcs "pj01.c")
endif()



idf_component_register(SRCS "qtz.c" "${srcs}"
                        INCLUDE_DIRS "include"
                        REQUIRES "mqtt" "json" "button" "Battery" "nvs_flash")







