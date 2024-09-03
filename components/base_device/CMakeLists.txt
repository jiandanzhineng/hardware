
set(BASE_COP "device_common" "mqtt" "json" "button" "Battery")
if(CONFIG_DEVICE_TD01)
    message(STATUS "CONFIG_DEVICE_TD01")
    set(COP "TD01")
elseif(CONFIG_DEVICE_TJS)
    set(COP "tjs")
elseif(CONFIG_DEVICE_DIANJI)
    set(COP "p_dianji")
elseif(CONFIG_DEVICE_QTZ)
    set(COP "qtz")
else()
    #message(FATAL_ERROR "Please select a device type")
endif()



if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(SRCS "base_device.c"
                            INCLUDE_DIRS "include"
                            REQUIRES "device_common" "mqtt" "json" "button" "Battery" "TD01" "tjs" "p_dianji" "qtz")
else()
    idf_component_register(SRCS "base_device.c"
                            INCLUDE_DIRS "include"
                            REQUIRES "device_common" "mqtt" "json" "button" "Battery" ${COP})
endif()

message(STATUS "COP: ${COP}")




