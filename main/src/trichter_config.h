#pragma once

#include "esp_log.h"
#include "sdkconfig.h"

#ifdef CONFIG_ENABLE_CAMERA
#define TRICHTER_CAMERA_ENABLED 1
#else
#define TRICHTER_CAMERA_ENABLED 0
#endif

#ifdef CONFIG_ENABLE_DISPLAY
#define TRICHTER_DISPLAY_ENABLED 1
#else
#define TRICHTER_DISPLAY_ENABLED 0
#endif

#ifdef CONFIG_ENABLE_SENSOR
#define TRICHTER_SENSOR_ENABLED 1
#else
#define TRICHTER_SENSOR_ENABLED 0
#endif

#ifdef CONFIG_ENABLE_WIFI
#define TRICHTER_WIFI_ENABLED 1
#else
#define TRICHTER_WIFI_ENABLED 0
#endif

#define TRICHTER_BLE_ENABLED 1

#define TRICHTER_SENSOR_GPIO CONFIG_SENSOR_GPIO
#define TRICHTER_SENSOR_STARTUP_PULSES CONFIG_SENSOR_STARTUP_PULSES
#define TRICHTER_SENSOR_STARTUP_WINDOW_MS CONFIG_SENSOR_STARTUP_WINDOW_MS
#define TRICHTER_SENSOR_IDLE_TIMEOUT_MS CONFIG_SENSOR_IDLE_TIMEOUT_MS
#define TRICHTER_SENSOR_GLITCH_NS CONFIG_SENSOR_GLITCH_NS

#define TRICHTER_MAIN_LOOP_DELAY_MS 100
#define TRICHTER_BLE_ACK_TIMEOUT_SECONDS 30
#define TRICHTER_STANDALONE_SESSION_DELAY_MS 5000
#define TRICHTER_ERROR_RECOVERY_DELAY_MS 1000

#define TRICHTER_LOGI(tag, fmt, ...)                                           \
  ESP_LOGI(tag, "(%s:%d) [TRICHTER] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define TRICHTER_LOGW(tag, fmt, ...)                                           \
  ESP_LOGW(tag, "(%s:%d) [TRICHTER] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define TRICHTER_LOGE(tag, fmt, ...)                                           \
  ESP_LOGE(tag, "(%s:%d) [TRICHTER] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define TRICHTER_LOGD(tag, fmt, ...)                                           \
  ESP_LOGD(tag, "(%s:%d) [TRICHTER] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#define TRICHTER_CONFIG_CHECK(condition, message)                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      ESP_LOGE("TRICHTER_CONFIG", "Configuration error: " message);            \
      return ESP_ERR_INVALID_STATE;                                            \
    }                                                                          \
  } while (0)

static inline bool trichter_is_camera_available(void) {
  return TRICHTER_CAMERA_ENABLED;
}

static inline bool trichter_is_display_available(void) {
  return TRICHTER_DISPLAY_ENABLED;
}

static inline bool trichter_is_sensor_available(void) {
  return TRICHTER_SENSOR_ENABLED;
}

static inline bool trichter_is_wifi_available(void) {
  return TRICHTER_WIFI_ENABLED;
}

static inline bool trichter_is_ble_available(void) {
  return TRICHTER_BLE_ENABLED;
}
