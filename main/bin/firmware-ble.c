#include "ble/ble.h"
#include "ble/gap.h"
#include "ble/gatt_svc.h"
#include "camera/camera.h"
#include "display/display.h"
#include "sensor/sensor.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "soc/gpio_num.h"
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <unistd.h>

static const char *TAG = "app_main";

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

static esp_err_t initialize_system(void) {
  ESP_LOGI(TAG, "Initializing system components...");

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(camera_init_module());
  ESP_ERROR_CHECK(sensor_init(GPIO_NUM_4));

  ESP_LOGI(TAG, "System initialization complete");
  return ESP_OK;
}

static void log_session_result(const SessionResult *session_result) {
  ESP_LOGI(
      TAG,
      "Session complete - Duration: %.2fs, Rate: %.2f L/min, Volume: %.2f L",
      session_result->duration_us / 1e6f, session_result->rate_lpm,
      session_result->volume_l);

  if (session_result->image_fb) {
    ESP_LOGI(TAG, "Image captured during session: %dx%d, %zu bytes",
             session_result->image_fb->width, session_result->image_fb->height,
             session_result->image_fb->len);
  } else {
    ESP_LOGW(TAG, "No image captured during session");
  }
}

static void process_session_result(SessionResult *session_result) {
  log_session_result(session_result);
  sensor_cleanup_session_result(session_result);
}

static esp_err_t run_measurement_session(SessionResult *session_result) {
  ESP_LOGI(TAG, "Awaiting session...");

  esp_err_t err = sensor_measure_session(session_result);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Session measurement failed: %s", esp_err_to_name(err));
    return err;
  }

  process_session_result(session_result);

  return ESP_OK;
}

void app_main(void) {
    /* Local variables */
    int rc;
    esp_err_t ret;

    /*
     * NVS flash initialization
     * Dependency of BLE stack to store configurations
     */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nvs flash, error code: %d ", ret);
        return;
    }

    /* NimBLE stack initialization */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nimble stack, error code: %d ",
                 ret);
        return;
    }

    /* GAP service initialization */
    rc = gap_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to initialize GAP service, error code: %d", rc);
        return;
    }

    /* GATT server initialization */
    rc = gatt_svc_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to initialize GATT server, error code: %d", rc);
        return;
    }

    /* NimBLE host configuration initialization */
    nimble_host_config_init();

    /* Start NimBLE host task thread and return */
    xTaskCreate(nimble_host_task, "NimBLE Host", 4*1024, NULL, 5, NULL);
    xTaskCreate(heart_rate_task, "Heart Rate", 4*1024, NULL, 5, NULL);
    return;

}

// void app_main(void) {
//   ESP_ERROR_CHECK(initialize_system());
//
//   lv_disp_t *disp = display_init();
//   SessionResult session_result;
//
//   while (true) {
//     display_write_await_session(disp);
//     esp_err_t err;
//     err = run_measurement_session(&session_result);
//     if (err != ESP_OK) {
//       ESP_LOGE(TAG, "Measurement session failed: %s", esp_err_to_name(err));
//       sleep(1);
//       continue;
//     }
//
//     display_write_result(disp, &session_result);
//     ESP_LOGI(TAG, "Waiting one second before next session...");
//     sleep(5);
//   }
// }
