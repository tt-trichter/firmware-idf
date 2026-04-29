#pragma once

#include "esp_camera.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "soc/gpio_num.h"
#include "trichter_error.h"
#include <stdint.h>

typedef struct {
  uint64_t duration_us;
  float rate_lpm;
  float volume_l;
  camera_fb_t *image_fb;
} SessionResult;

esp_err_t sensor_init(gpio_num_t pulse_gpio);

// Arm the sensor to detect the start of a new session.
// Enables the GPIO interrupt, resets and starts the pulse counter.
// Idempotent — safe to call every loop tick; only arms once until triggered.
esp_err_t sensor_arm(void);

// Non-blocking check: returns true once the first rising-edge pulse has been
// detected since the last sensor_arm() call. Consumes the trigger so
// subsequent calls return false until sensor_arm() is called again.
bool sensor_poll_triggered(void);

// Run the active measurement phase. Must be called after sensor_poll_triggered()
// returns true. Validates the startup window, then measures flow until idle
// timeout, then returns the result.
esp_err_t sensor_measure_session(SessionResult *out_result);

void sensor_cleanup_session_result(SessionResult *result);
