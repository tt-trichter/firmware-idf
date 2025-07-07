#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif_types.h"
#include "esp_wifi_types_generic.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "camera.h"
#include "sensor.h"
#include "server.h"
#include "soc/gpio_num.h"
#include "wifi.h"

static const char *TAG = "app_main";

static void on_wifi_connect(void *arg, esp_event_base_t event_base,
                            int32_t event_id, void *event_data) {
  httpd_handle_t *server = (httpd_handle_t *)arg;
  if (*server == NULL) {
    ESP_LOGI(TAG, "Wi-Fi connected, starting HTTP server");
    *server = server_start();
  }
}

static void on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  httpd_handle_t *server = arg;
  if (*server) {
    ESP_LOGI(TAG, "Wi-Fi lost, stopping HTTP server");
    server_stop(*server);
    *server = NULL;
  }
}

void app_main(void) {

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(camera_init_module());
  ESP_ERROR_CHECK(wifi_init_sta());
  ESP_ERROR_CHECK(sensor_init(GPIO_NUM_4));

  static httpd_handle_t server = NULL;

  /* register our start/stop callbacks */
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &on_wifi_connect, &server));
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, &server));

    SessionResult session_result;
    sensor_measure_session(&session_result);
  server = server_start();

  while (server) {
    sleep(5);
  }
}
