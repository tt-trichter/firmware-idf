#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "soc/gpio_num.h"

#define PULSES_PER_LITER 6.6f

typedef struct
{
    uint64_t duration_us;
    float rate_lpm;
    float volume_l;
    camera_fb_t *image_fb; // Camera frame buffer captured during session
} SessionResult;

esp_err_t sensor_init(gpio_num_t pulse_gpio);

esp_err_t sensor_measure_session(SessionResult *out_result);

/**
 * @brief Clean up resources in SessionResult
 * @param result Pointer to SessionResult to clean up
 * @note This function releases the camera frame buffer if present
 */
void sensor_cleanup_session_result(SessionResult *result);

#ifdef CONFIG_SENSOR_ENABLE_SIMULATION
/**
 * @brief Simulate a sensor session for testing
 * @param pulse_count Number of pulses to simulate
 * @param duration_ms Duration of the simulated session in milliseconds
 * @param out_result Pointer to store the session result
 * @return ESP_OK on success
 */
esp_err_t sensor_simulate_session(int pulse_count, uint32_t duration_ms, SessionResult *out_result);
#endif
