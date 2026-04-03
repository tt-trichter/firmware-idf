# Collect source files from active modules
file(GLOB_RECURSE BLE_SRC_FILES "src/ble/*.c")
file(GLOB_RECURSE CAMERA_SRC_FILES "src/camera/*.c")
file(GLOB_RECURSE DISPLAY_SRC_FILES "src/display/*.c")
file(GLOB_RECURSE SENSOR_SRC_FILES "src/sensor/*.c")

# Combine all source files
set(ALL_SRC_FILES
    ${BLE_SRC_FILES}
    ${CAMERA_SRC_FILES}
    ${DISPLAY_SRC_FILES}
    ${SENSOR_SRC_FILES}
)

idf_component_register(
    SRCS 
       "src/main.c"
       ${ALL_SRC_FILES}
    INCLUDE_DIRS "src"
    PRIV_REQUIRES 
      bt 
      esp-tls
      esp_driver_gpio
      esp_driver_pcnt
      esp_http_client
      esp_http_server
      esp_netif
      esp_timer
      esp_wifi
      json
      nvs_flash
)
