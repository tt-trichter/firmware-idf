#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "trichter_config.h"

typedef enum {
  TRICHTER_ERR_NONE = 0,
  TRICHTER_ERR_INIT,
  TRICHTER_ERR_SENSOR,
  TRICHTER_ERR_CAMERA,
  TRICHTER_ERR_DISPLAY,
  TRICHTER_ERR_BLE,
  TRICHTER_ERR_STATE,
  TRICHTER_ERR_MEMORY,
  TRICHTER_ERR_TIMEOUT,
  TRICHTER_ERR_CONFIG,
  TRICHTER_ERR_UNKNOWN
} trichter_error_category_t;

typedef struct {
  trichter_error_category_t category;
  esp_err_t esp_error;
  const char *description;
  const char *file;
  int line;
  uint32_t timestamp;
} trichter_error_info_t;

typedef enum {
  TRICHTER_RECOVERY_NONE,
  TRICHTER_RECOVERY_RETRY,
  TRICHTER_RECOVERY_RESET_MODULE,
  TRICHTER_RECOVERY_RESTART,
  TRICHTER_RECOVERY_SAFE_MODE
} trichter_recovery_action_t;

#define TRICHTER_CHECK(condition, category, description)                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      trichter_error_log(category, ESP_FAIL, description, __FILE__, __LINE__); \
      return ESP_FAIL;                                                         \
    }                                                                          \
  } while (0)

#define TRICHTER_CHECK_ERR(err, category, description)                         \
  do {                                                                         \
    esp_err_t _err = (err);                                                    \
    if (_err != ESP_OK) {                                                      \
      trichter_error_log(category, _err, description, __FILE__, __LINE__);     \
      return _err;                                                             \
    }                                                                          \
  } while (0)

#define TRICHTER_CHECK_GOTO(condition, category, description, label)           \
  do {                                                                         \
    if (!(condition)) {                                                        \
      trichter_error_log(category, ESP_FAIL, description, __FILE__, __LINE__); \
      goto label;                                                              \
    }                                                                          \
  } while (0)

#define TRICHTER_LOG_ERROR(category, err, description)                         \
  trichter_error_log(category, err, description, __FILE__, __LINE__)

static inline void trichter_error_log(trichter_error_category_t category,
                                      esp_err_t esp_error,
                                      const char *description, const char *file,
                                      int line) {
  const char *category_str;

  switch (category) {
  case TRICHTER_ERR_INIT:
    category_str = "INIT";
    break;
  case TRICHTER_ERR_SENSOR:
    category_str = "SENSOR";
    break;
  case TRICHTER_ERR_CAMERA:
    category_str = "CAMERA";
    break;
  case TRICHTER_ERR_DISPLAY:
    category_str = "DISPLAY";
    break;
  case TRICHTER_ERR_BLE:
    category_str = "BLE";
    break;
  case TRICHTER_ERR_STATE:
    category_str = "STATE";
    break;
  case TRICHTER_ERR_MEMORY:
    category_str = "MEMORY";
    break;
  case TRICHTER_ERR_TIMEOUT:
    category_str = "TIMEOUT";
    break;
  case TRICHTER_ERR_CONFIG:
    category_str = "CONFIG";
    break;
  default:
    category_str = "UNKNOWN";
    break;
  }

  ESP_LOGE("TRICHTER_ERROR", "[%s] %s (%s:%d) - ESP_ERR: %s (0x%x)",
           category_str, description, file, line, esp_err_to_name(esp_error),
           esp_error);
}

static inline trichter_recovery_action_t
trichter_get_recovery_action(trichter_error_category_t category) {
  switch (category) {
  case TRICHTER_ERR_INIT:
  case TRICHTER_ERR_CONFIG:
    return TRICHTER_RECOVERY_RESTART;

  case TRICHTER_ERR_SENSOR:
  case TRICHTER_ERR_CAMERA:
  case TRICHTER_ERR_DISPLAY:
  case TRICHTER_ERR_BLE:
    return TRICHTER_RECOVERY_RESET_MODULE;

  case TRICHTER_ERR_TIMEOUT:
    return TRICHTER_RECOVERY_RETRY;

  case TRICHTER_ERR_MEMORY:
    return TRICHTER_RECOVERY_SAFE_MODE;

  case TRICHTER_ERR_STATE:
  default:
    return TRICHTER_RECOVERY_NONE;
  }
}

static inline bool
trichter_is_error_recoverable(trichter_error_category_t category) {
  return trichter_get_recovery_action(category) != TRICHTER_RECOVERY_NONE;
}
