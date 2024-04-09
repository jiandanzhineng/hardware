if(CONFIG_DEVICE_TD01)
    set(REQUIRES "TD01")
elseif(CONFIG_DEVICE_TJS)
    set(REQUIRES "tjs")
elseif(CONFIG_DEVICE_DIANJI)
    set(REQUIRES "p_dianji")
elseif(CONFIG_DEVICE_QTZ)
    set(REQUIRES "qtz")
endif()

idf_component_register(SRCS "base_device.c"
                        INCLUDE_DIRS "include"
                        REQUIRES "device_common" "mqtt" "json" "button" "qtz" ${REQUIRES})



