#include "display.h"
#include "esp_camera.h"
#include "esp_event.h"
#include "esp_http_client.h"
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
#include "http_client.h"

static const char *TAG = "app_main";

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

static esp_err_t initialize_system(void)
{
  ESP_LOGI(TAG, "Initializing system components...");

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(camera_init_module());
  ESP_ERROR_CHECK(wifi_init_sta());
  ESP_ERROR_CHECK(sensor_init(GPIO_NUM_4));
  ESP_ERROR_CHECK(http_client_init());

  ESP_LOGI(TAG, "System initialization complete");
  return ESP_OK;
}

static esp_err_t submit_session_to_server(const SessionResult *session_result)
{
  if (!session_result)
  {
    ESP_LOGE(TAG, "Invalid session result");
    return ESP_ERR_INVALID_ARG;
  }

  if (!session_result->image_fb)
  {
    ESP_LOGW(TAG, "No image available for session, skipping server submission");
    return ESP_ERR_INVALID_ARG;
  }

  session_data_t session_data = {
      .rate = session_result->rate_lpm,
      .duration = session_result->duration_us / 1e6f,
      .volume = session_result->volume_l,
      .image_fb = session_result->image_fb};

  ESP_LOGI(TAG, "Submitting session to server: Rate=%.2f L/min, Duration=%.2fs, Volume=%.2f L",
           session_data.rate, session_data.duration, session_data.volume);

  esp_err_t err = http_client_submit_session(&session_data, NULL, NULL);

  if (err == ESP_OK)
  {
    ESP_LOGI(TAG, "Session submitted successfully to server");
  }
  else
  {
    ESP_LOGE(TAG, "Failed to submit session to server: %s", esp_err_to_name(err));
  }

  return err;
}

static void log_session_result(const SessionResult *session_result)
{
  ESP_LOGI(TAG, "Session complete - Duration: %.2fs, Rate: %.2f L/min, Volume: %.2f L",
           session_result->duration_us / 1e6f,
           session_result->rate_lpm, session_result->volume_l);

  if (session_result->image_fb)
  {
    ESP_LOGI(TAG, "Image captured during session: %dx%d, %zu bytes",
             session_result->image_fb->width,
             session_result->image_fb->height,
             session_result->image_fb->len);
  }
  else
  {
    ESP_LOGW(TAG, "No image captured during session");
  }
}

static void process_session_result(SessionResult *session_result)
{
  log_session_result(session_result);

  esp_err_t submit_err = submit_session_to_server(session_result);
  if (submit_err != ESP_OK)
  {
    ESP_LOGW(TAG, "Failed to submit session to server, continuing...");
  }

  sensor_cleanup_session_result(session_result);
}

static esp_err_t run_measurement_session(SessionResult *session_result)
{
  ESP_LOGI(TAG, "Measuring session...");

  esp_err_t err = sensor_measure_session(session_result);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Session measurement failed: %s", esp_err_to_name(err));
    return err;
  }

  process_session_result(session_result);

  return ESP_OK;
}

void app_main(void)
{
  ESP_ERROR_CHECK(initialize_system());

  lv_disp_t *disp = display_init();
  SessionResult session_result;

  while (true)
  {
  display_write_await_session(disp);
    esp_err_t err;
    err = run_measurement_session(&session_result);
    if (err != ESP_OK)
    {
      ESP_LOGE(TAG, "Measurement session failed: %s", esp_err_to_name(err));
      sleep(1);
      continue;
    }

    display_write_result(disp, &session_result);
    ESP_LOGI(TAG, "Waiting one second before next session...");
    sleep(5);
  }
}
