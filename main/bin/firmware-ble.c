#include "ble/ble.h"
#include "ble/gap.h"
#include "ble/gatt_svc.h"
#include "ble/trichter_service.h"
#include "freertos/projdefs.h"
#include "sensor/sensor.h"
#include "trichter_config.h"
#include "trichter_state.h"

#if TRICHTER_CAMERA_ENABLED
#include "camera/camera.h"
#endif

#if TRICHTER_DISPLAY_ENABLED
#include "display/display.h"
#endif

#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "soc/gpio_num.h"
#include <string.h>
#include <sys/param.h>

static const char *TAG = "trichter_main";

static app_context_t app_ctx = {.state = APP_STATE_INIT,
                                .ble_enabled = TRICHTER_BLE_ENABLED,
                                .ble_connected = false,
                                .camera_enabled = TRICHTER_CAMERA_ENABLED,
                                .display_enabled = TRICHTER_DISPLAY_ENABLED,
                                .sensor_enabled = TRICHTER_SENSOR_ENABLED};

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
    ret = sensor_init(GPIO_NUM_4);
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

  app_state_set(&app_ctx, APP_STATE_READY);
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
    app_ctx.state = APP_STATE_ERROR;
    return;
  }

  log_session_result(session_result);

  memcpy(&app_ctx.current_session, session_result, sizeof(SessionResult));
  app_state_set(&app_ctx, APP_STATE_SESSION_COMPLETE);

  if (app_ctx.ble_enabled && trichter_ble_is_connected()) {
    TRICHTER_LOGI(TAG, "Sending session result via BLE...");

    trichter_ble_set_status(TRICHTER_STATUS_COMPLETE);
    trichter_ble_send_result(session_result);

    int timeout_count = 0;

    while (trichter_ble_is_waiting_for_ack() &&
           timeout_count < TRICHTER_BLE_ACK_TIMEOUT_SECONDS) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      timeout_count++;
    }

    if (trichter_ble_is_waiting_for_ack()) {
      TRICHTER_LOGW(TAG, "BLE acknowledgment timeout after %d seconds",
                    TRICHTER_BLE_ACK_TIMEOUT_SECONDS);
    } else {
      TRICHTER_LOGI(TAG, "BLE acknowledgment received");
    }

    trichter_ble_cleanup_result();
  }

  sensor_cleanup_session_result(session_result);

  app_state_set(&app_ctx, APP_STATE_WAITING_SESSION);
}

static esp_err_t run_measurement_session(SessionResult *session_result) {
  if (!session_result) {
    TRICHTER_LOGE(TAG, "Invalid session result pointer");
    return ESP_ERR_INVALID_ARG;
  }

  if (!app_ctx.sensor_enabled) {
    TRICHTER_LOGE(TAG, "Cannot run session - sensor not enabled");
    return ESP_ERR_INVALID_STATE;
  }

  TRICHTER_LOGI(TAG, "Awaiting for flow to start...");

  app_state_set(&app_ctx, APP_STATE_SESSION_RUNNING);

  if (app_ctx.ble_enabled && app_ctx.ble_connected) {
    trichter_ble_set_status(TRICHTER_STATUS_RUNNING);
  }

  esp_err_t err = sensor_measure_session(session_result);
  if (err != ESP_OK) {
    TRICHTER_LOGE(TAG, "Session measurement failed: %s", esp_err_to_name(err));
    app_state_set(&app_ctx, APP_STATE_ERROR);

    if (app_ctx.ble_enabled && app_ctx.ble_connected) {
      trichter_ble_set_status(TRICHTER_STATUS_ERROR);
    }

    return err;
  }

  process_session_result(session_result);
  return ESP_OK;
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
    app_state_set(&app_ctx, APP_STATE_WAITING_SESSION);
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
    app_state_set(&app_ctx, APP_STATE_WAITING_SESSION);
  }
}

static bool should_start_session(void) {
  // Check if system is ready to start a session
  if (!app_can_start_session(&app_ctx)) {
    return false;
  }

  // Always ready to start if sensor is enabled - sensor will block until flow
  // is detected
  return true;
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
  lv_disp_t *disp = NULL;
  if (trichter_is_display_available()) {
    disp = display_init();
    if (disp) {
      TRICHTER_LOGI(TAG, "Display initialized successfully");
      display_show_icon(disp);
    } else {
      TRICHTER_LOGW(TAG, "Display initialization failed");
      app_ctx.display_enabled = false;
    }
  }
#else
  void *disp = NULL;
#endif

  app_state_set(&app_ctx, APP_STATE_WAITING_SESSION);
  SessionResult session_result;

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
      if (app_ctx.display_enabled && disp) {
#if TRICHTER_DISPLAY_ENABLED
        display_write_await_session(disp);
#endif
      }

      if (app_ctx.ble_enabled && app_ctx.ble_connected) {
        trichter_ble_set_status(TRICHTER_STATUS_WAITING);
      }

      if (should_start_session()) {
        TRICHTER_LOGI(
            TAG,
            "Ready to start session - waiting for sensor flow detection...");

        esp_err_t err = run_measurement_session(&session_result);
        if (err != ESP_OK) {
          TRICHTER_LOGE(TAG, "Session failed: %s", esp_err_to_name(err));

          if (app_ctx.ble_enabled && app_ctx.ble_connected) {
            trichter_ble_set_status(TRICHTER_STATUS_ERROR);
          }

          vTaskDelay(pdMS_TO_TICKS(TRICHTER_ERROR_RECOVERY_DELAY_MS));
          app_state_set(&app_ctx, APP_STATE_WAITING_SESSION);
        }
      }
      break;

    case APP_STATE_SESSION_COMPLETE:
      if (app_ctx.display_enabled && disp) {
#if TRICHTER_DISPLAY_ENABLED
        display_write_result(disp, &app_ctx.current_session);
#endif
      }

      if (app_ctx.ble_enabled && app_ctx.ble_connected) {
        if (!trichter_ble_is_waiting_for_ack()) {
          app_state_set(&app_ctx, APP_STATE_WAITING_SESSION);
        }
      } else {
        TRICHTER_LOGI(
            TAG, "Standalone mode - waiting %d seconds before next session",
            TRICHTER_STANDALONE_SESSION_DELAY_MS / 1000);
        vTaskDelay(pdMS_TO_TICKS(TRICHTER_STANDALONE_SESSION_DELAY_MS));
        app_state_set(&app_ctx, APP_STATE_WAITING_SESSION);
      }
      break;

    case APP_STATE_ERROR:
      TRICHTER_LOGE(TAG, "System in error state - attempting recovery");
      vTaskDelay(pdMS_TO_TICKS(TRICHTER_ERROR_RECOVERY_DELAY_MS));
      app_state_set(&app_ctx, APP_STATE_WAITING_SESSION);
      break;

    default:
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(TRICHTER_MAIN_LOOP_DELAY_MS));
  }
}
