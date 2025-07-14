idf_component_register(
    SRCS 
      # "bin/scanner.c"
       "main.c"
       "src/server.c"
       "src/wifi.c"
       "src/camera.c"
       "src/sensor.c"
       "src/display.c"
       "src/http_client.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES 
      esp_wifi
      nvs_flash
      esp_timer
      esp-tls
      nvs_flash
      esp_netif
      esp_http_server
      esp_http_client
      esp_driver_pcnt
      json
)
