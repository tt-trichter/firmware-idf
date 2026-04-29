#include "ble/ble.h"
#include "ble/gap.h"
#include "ble/gatt_svc.h"
#include "ble/trichter_service.h"
#include "esp_camera.h"
#include "freertos/projdefs.h"
#include "host/ble_att.h"
#include "sdkconfig.h"
#include "sensor/sensor.h"
#include "trichter_config.h"
#include "trichter_state.h"

#if TRICHTER_CAMERA_ENABLED
#include "camera/camera.h"
#endif

#if TRICHTER_DISPLAY_ENABLED
#include "display/display.h"
#endif

#if TRICHTER_LED_STATUS_ENABLED
#include "led_status/led_status.h"
#endif

#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "soc/gpio_num.h"
#include <string.h>
#include <sys/param.h>

static const char *TAG = "trichter_main";

#if TRICHTER_DISPLAY_ENABLED
static lv_disp_t *s_disp = NULL;
#else
static void *s_disp = NULL;
#endif
static app_state_t s_last_displayed = APP_STATE_INIT;

static app_context_t app_ctx = {.state = APP_STATE_INIT,
                                .ble_enabled = TRICHTER_BLE_ENABLED,
                                .ble_connected = false,
                                .camera_enabled = TRICHTER_CAMERA_ENABLED,
                                .display_enabled = TRICHTER_DISPLAY_ENABLED,
                                .sensor_enabled = TRICHTER_SENSOR_ENABLED};

static TaskHandle_t s_fake_run_task = NULL;

// Wrapper: validates state transition, updates LED, and logs in one call.
// Use this everywhere instead of calling app_state_set() directly.
static void set_state(app_state_t new_state) {
  app_state_set(&app_ctx, new_state);
#if TRICHTER_LED_STATUS_ENABLED
  led_status_update_from_state(new_state);
#endif
}

static esp_err_t initialize_system_components(void) {
  TRICHTER_LOGI(TAG, "Initializing system components...");
  esp_err_t ret = ESP_OK;

  TRICHTER_CONFIG_CHECK(TRICHTER_SENSOR_ENABLED,
                        "Sensor must be enabled for this firmware");

  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    TRICHTER_CHECK_ERR(nvs_flash_erase(), TRICHTER_ERR_INIT,
                       "Failed to erase NVS flash");
    ret = nvs_flash_init();
  }
  TRICHTER_CHECK_ERR(ret, TRICHTER_ERR_INIT, "Failed to initialize NVS flash");

  if (trichter_is_sensor_available()) {
    ret = sensor_init(TRICHTER_SENSOR_GPIO);
    if (ret == ESP_OK) {
      app_ctx.sensor_enabled = true;
      TRICHTER_LOGI(TAG, "Sensor initialized successfully");
    } else {
      TRICHTER_LOGE(TAG, "Failed to initialize sensor: %s",
                    esp_err_to_name(ret));
      return ret;
    }
  }

#if TRICHTER_CAMERA_ENABLED
  if (trichter_is_camera_available()) {
    ret = camera_init_module();
    if (ret == ESP_OK) {
      app_ctx.camera_enabled = true;
      TRICHTER_LOGI(TAG, "Camera initialized successfully");
    } else {
      TRICHTER_LOGE(TAG, "Failed to initialize camera: %s",
                    esp_err_to_name(ret));
      app_ctx.camera_enabled = false;
    }
  }
#else
  TRICHTER_LOGI(TAG, "Camera disabled in configuration");
  app_ctx.camera_enabled = false;
#endif

#if TRICHTER_DISPLAY_ENABLED
  if (trichter_is_display_available()) {
    app_ctx.display_enabled = true;
    TRICHTER_LOGI(TAG, "Display will be initialized");
  }
#else
  TRICHTER_LOGI(TAG, "Display disabled in configuration");
  app_ctx.display_enabled = false;
#endif

  set_state( APP_STATE_READY);
  TRICHTER_LOGI(TAG, "System initialization complete");

  return ESP_OK;
}

static void log_session_result(const SessionResult *session_result) {
  if (!session_result) {
    TRICHTER_LOGE(TAG, "Invalid session result pointer");
    return;
  }

  TRICHTER_LOGI(TAG, "=== Session Results ===");
  TRICHTER_LOGI(TAG, "Duration: %.2fs", session_result->duration_us / 1e6f);
  TRICHTER_LOGI(TAG, "Flow rate: %.2f L/min", session_result->rate_lpm);
  TRICHTER_LOGI(TAG, "Total volume: %.2f L", session_result->volume_l);

  if (app_ctx.camera_enabled && session_result->image_fb) {
    TRICHTER_LOGI(
        TAG, "Image: %dx%d, %zu bytes", session_result->image_fb->width,
        session_result->image_fb->height, session_result->image_fb->len);
  } else if (app_ctx.camera_enabled) {
    TRICHTER_LOGW(TAG, "Camera enabled but no image captured");
  } else {
    TRICHTER_LOGI(TAG, "Camera disabled - no image data");
  }
  TRICHTER_LOGI(TAG, "=====================");
}

static void process_session_result(SessionResult *session_result) {
  if (!session_result) {
    TRICHTER_LOGE(TAG, "Invalid session result pointer");
    set_state(APP_STATE_ERROR);
    return;
  }

  log_session_result(session_result);

  memcpy(&app_ctx.current_session, session_result, sizeof(SessionResult));
  set_state( APP_STATE_SESSION_COMPLETE);

#if TRICHTER_DISPLAY_ENABLED
  if (app_ctx.display_enabled && s_disp) {
    display_write_result(s_disp, session_result);
    s_last_displayed = APP_STATE_SESSION_COMPLETE;
  }
#endif

  if (app_ctx.ble_enabled && trichter_ble_is_connected()) {
    TRICHTER_LOGI(TAG, "Sending session result via BLE...");

    trichter_ble_set_status(TRICHTER_STATUS_COMPLETE);

    trichter_ble_send_result(session_result);

    int timeout_count = 0;

    // bool was_connected_at_start = trichter_ble_is_connected();

    while (trichter_ble_is_waiting_for_ack() &&
           timeout_count < TRICHTER_BLE_ACK_TIMEOUT_SECONDS &&
           trichter_ble_is_connected()) {
      while (app_ctx.state == APP_STATE_TRANSFERRING_IMAGE) {
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
      TRICHTER_LOGI(TAG, "Awaiting ble ack...");
      vTaskDelay(pdMS_TO_TICKS(1000));
      timeout_count++;
    }

    if (!trichter_ble_is_connected()) {
      TRICHTER_LOGW(TAG, "BLE disconnected before ACK was received");
    } else if (trichter_ble_is_waiting_for_ack()) {
      TRICHTER_LOGW(TAG, "BLE acknowledgment timeout after %d seconds",
                    TRICHTER_BLE_ACK_TIMEOUT_SECONDS);
    } else {
      TRICHTER_LOGI(TAG, "BLE acknowledgment received");
    }

    trichter_ble_cleanup_result();
  }

  sensor_cleanup_session_result(session_result);

  s_last_displayed =
      APP_STATE_INIT; // force display redraw on next WAITING_SESSION
  set_state( APP_STATE_WAITING_SESSION);
}

static esp_err_t run_measurement_session(SessionResult *session_result) {
  TRICHTER_LOGI(TAG, "Flow detected — starting active measurement");

  set_state( APP_STATE_SESSION_RUNNING);

  if (app_ctx.ble_enabled && app_ctx.ble_connected) {
    trichter_ble_set_status(TRICHTER_STATUS_RUNNING);
  }

  esp_err_t err = sensor_measure_session(session_result);
  if (err != ESP_OK) {
    TRICHTER_LOGE(TAG, "Session measurement failed: %s", esp_err_to_name(err));
    set_state( APP_STATE_ERROR);

    if (app_ctx.ble_enabled && app_ctx.ble_connected) {
      trichter_ble_set_status(TRICHTER_STATUS_ERROR);
    }

    return err;
  }

  process_session_result(session_result);
  return ESP_OK;
}

static void run_fake_session(SessionResult *session_result) {
  TRICHTER_LOGI(TAG, "Starting fake session");

  // if(esp_camera_available_frames()) {
  //   TRICHTER_LOGE(TAG, "THERE ARE FRAMES AVAILABLE!!!!");
  //   return;
  // }

  set_state( APP_STATE_SESSION_RUNNING);

  if (app_ctx.ble_enabled && app_ctx.ble_connected) {
    trichter_ble_set_status(TRICHTER_STATUS_RUNNING);
  }

  session_result->duration_us = 5000000;
  session_result->rate_lpm = 12.5f;
  session_result->volume_l = 1.04f;
  session_result->image_fb = NULL;

#if TRICHTER_CAMERA_ENABLED
  if (app_ctx.camera_enabled) {
    session_result->image_fb = camera_capture_frame();
    TRICHTER_LOGI(TAG, "Camera took picture! Size: %dx%d",
                  session_result->image_fb->height,
                  session_result->image_fb->width);
  }
#endif

  process_session_result(session_result);
}

static void ble_session_control_callback(trichter_control_cmd_t cmd) {
  switch (cmd) {
  case TRICHTER_CMD_ACKNOWLEDGE:
    TRICHTER_LOGI(TAG, "BLE result acknowledged");
    trichter_ble_cleanup_result();
    break;

  case TRICHTER_CMD_RESET:
    TRICHTER_LOGI(TAG, "BLE reset requested");
    trichter_ble_cleanup_result();
    trichter_ble_set_status(TRICHTER_STATUS_IDLE);
    set_state( APP_STATE_WAITING_SESSION);
    break;

  case TRICHTER_CMD_FAKE_RUN:
    if (app_ctx.state == APP_STATE_WAITING_SESSION) {
      TRICHTER_LOGI(TAG, "Fake run requested via BLE");
      xTaskNotifyGive(s_fake_run_task);
    } else {
      TRICHTER_LOGW(TAG, "Not in Waiting Mode! - Running fake run anyway");
      xTaskNotifyGive(s_fake_run_task);
    }
    break;

  case TRICHTER_CMD_IMAGE_START:
    TRICHTER_LOGI(TAG, "Transferring image...");
    set_state( APP_STATE_TRANSFERRING_IMAGE);
    break;

  case TRICHTER_CMD_IMAGE_ACK:
    TRICHTER_LOGI(TAG, "Still transferring image...");
    break;

  case TRICHTER_CMD_IMAGE_CANCEL:
    TRICHTER_LOGW(TAG, "Image transfer canceled!");
    set_state( APP_STATE_SESSION_COMPLETE);
    break;

  case TRICHTER_CMD_IMAGE_RECEIVED:
    TRICHTER_LOGI(TAG, "Image transfer complete");
    set_state( APP_STATE_SESSION_COMPLETE);
    break;

  default:
    TRICHTER_LOGW(TAG, "Unknown BLE command: %d", cmd);
    break;
  }
}

static esp_err_t initialize_ble_stack(void) {
  esp_err_t ret;
  int rc;

  TRICHTER_LOGI(TAG, "Initializing BLE stack...");

  ret = nimble_port_init();
  if (ret != ESP_OK) {
    TRICHTER_LOGE(TAG, "Failed to initialize nimble stack: %s",
                  esp_err_to_name(ret));
    return ret;
  }

  rc = gap_init();
  if (rc != 0) {
    TRICHTER_LOGE(TAG, "Failed to initialize GAP service, error code: %d", rc);
    return ESP_FAIL;
  }

  rc = gatt_svc_init();
  if (rc != 0) {
    TRICHTER_LOGE(TAG, "Failed to initialize GATT server, error code: %d", rc);
    return ESP_FAIL;
  }

  trichter_ble_set_session_callback(ble_session_control_callback);

  nimble_host_config_init();

  rc = ble_att_set_preferred_mtu(256);
  if (rc != 0) {
    ESP_LOGW(TAG, "Failed to set preferred MTU: %d", rc);
  } else {
    ESP_LOGI(TAG, "Preferred ATT MTU set to %u", ble_att_preferred_mtu());
  }

  xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);

  app_ctx.ble_enabled = true;
  TRICHTER_LOGI(TAG, "BLE stack initialized successfully");
  return ESP_OK;
}

static void update_ble_connection_state(void) {
  if (!app_ctx.ble_enabled) {
    return;
  }

  bool was_connected = app_ctx.ble_connected;
  app_ctx.ble_connected = trichter_ble_is_connected();

  if (app_ctx.ble_connected && !was_connected) {
    TRICHTER_LOGI(TAG, "BLE client connected - switching to BLE mode");
    trichter_ble_set_status(TRICHTER_STATUS_IDLE);
  } else if (!app_ctx.ble_connected && was_connected) {
    TRICHTER_LOGI(TAG,
                  "BLE client disconnected - switching to standalone mode");
    set_state( APP_STATE_WAITING_SESSION);
  }
}

static void fake_run_task(void *arg) {
  SessionResult session_result;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

#if TRICHTER_DISPLAY_ENABLED
    if (app_ctx.display_enabled && s_disp) {
      display_write_measuring(s_disp);
      s_last_displayed = APP_STATE_SESSION_RUNNING;
    }
#endif

    run_fake_session(&session_result);
  }
}

void app_main(void) {
  TRICHTER_LOGI(TAG, "=== Trichter Firmware Starting ===");

  esp_err_t init_ret = initialize_system_components();
  if (init_ret != ESP_OK) {
    TRICHTER_LOG_ERROR(TRICHTER_ERR_INIT, init_ret,
                       "System components initialization failed");
    // For critical initialization failure, we can't continue
    return;
  }

  esp_err_t ble_ret = initialize_ble_stack();
  if (ble_ret != ESP_OK) {
    TRICHTER_LOG_ERROR(TRICHTER_ERR_BLE, ble_ret, "BLE initialization failed");
    TRICHTER_LOGI(TAG, "Continuing in standalone mode without BLE");
    app_ctx.ble_enabled = false;
  }

#if TRICHTER_DISPLAY_ENABLED
  if (trichter_is_display_available()) {
    s_disp = display_init();
    if (s_disp) {
      TRICHTER_LOGI(TAG, "Display initialized successfully");
      display_show_icon(s_disp);
    } else {
      TRICHTER_LOGW(TAG, "Display initialization failed");
      app_ctx.display_enabled = false;
    }
  }
#endif

#if TRICHTER_LED_STATUS_ENABLED
  esp_err_t led_ret = led_status_init(TRICHTER_LED_STATUS_GPIO);
  if (led_ret != ESP_OK) {
    TRICHTER_LOGW(TAG, "LED status init failed: %s — continuing without LED",
                  esp_err_to_name(led_ret));
  }
#endif

  set_state( APP_STATE_WAITING_SESSION);
  SessionResult session_result;

  TRICHTER_LOGI(TAG, "Creating fake task...");
  BaseType_t rc = xTaskCreate(fake_run_task, "fake_run", 4 * 1024, NULL, 5,
                              &s_fake_run_task);
  if (rc != pdPASS) {
    TRICHTER_LOGE(TAG, "Failed to create fake run task");
    return;
  }

  TRICHTER_LOGI(TAG, "=== System Ready ===");
  TRICHTER_LOGI(TAG, "State: %s", app_state_to_string(app_ctx.state));
  TRICHTER_LOGI(TAG, "BLE: %s", app_ctx.ble_enabled ? "enabled" : "disabled");
  TRICHTER_LOGI(TAG, "Camera: %s",
                app_ctx.camera_enabled ? "enabled" : "disabled");
  TRICHTER_LOGI(TAG, "Display: %s",
                app_ctx.display_enabled ? "enabled" : "disabled");
  TRICHTER_LOGI(TAG, "Sensor: %s",
                app_ctx.sensor_enabled ? "enabled" : "disabled");

  trichter_ble_print_state();
  while (true) {
    update_ble_connection_state();

    switch (app_ctx.state) {
    case APP_STATE_WAITING_SESSION:
#if TRICHTER_DISPLAY_ENABLED
      if (app_ctx.display_enabled && s_disp &&
          s_last_displayed != APP_STATE_WAITING_SESSION) {
        display_write_await_session(s_disp);
        s_last_displayed = APP_STATE_WAITING_SESSION;
      }
#endif

      if (app_ctx.ble_enabled && app_ctx.ble_connected) {
        trichter_ble_set_status(TRICHTER_STATUS_WAITING);
      }

      if (app_ctx.sensor_enabled) {
        // Arm is idempotent — only enables the interrupt if not already armed
        sensor_arm();

        if (sensor_poll_triggered()) {
          TRICHTER_LOGI(TAG, "First pulse received — entering active measurement");

#if TRICHTER_DISPLAY_ENABLED
          if (app_ctx.display_enabled && s_disp) {
            display_write_measuring(s_disp);
            s_last_displayed = APP_STATE_SESSION_RUNNING;
          }
#endif

          esp_err_t err = run_measurement_session(&session_result);
          if (err != ESP_OK) {
            TRICHTER_LOGE(TAG, "Session failed: %s — re-arming sensor",
                          esp_err_to_name(err));

            if (app_ctx.ble_enabled && app_ctx.ble_connected) {
              trichter_ble_set_status(TRICHTER_STATUS_ERROR);
            }

            s_last_displayed = APP_STATE_INIT;
            vTaskDelay(pdMS_TO_TICKS(TRICHTER_ERROR_RECOVERY_DELAY_MS));
            set_state( APP_STATE_WAITING_SESSION);
          }
        }
      }
      break;

    case APP_STATE_ERROR:
      TRICHTER_LOGE(TAG, "System in error state - attempting recovery");
      vTaskDelay(pdMS_TO_TICKS(TRICHTER_ERROR_RECOVERY_DELAY_MS));
      set_state( APP_STATE_WAITING_SESSION);
      break;

    default:
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(TRICHTER_MAIN_LOOP_DELAY_MS));
  }
}
