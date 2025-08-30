file(GLOB_RECURSE SRC_FILES "src/*.c")

idf_component_register(
    SRCS 
       "bin/firmware-ble.c"
       ${SRC_FILES}
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
