#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_timer.h"
#include "soc/gpio_num.h"

#define PULSES_PER_LITER 6.6f

typedef struct {
    uint64_t duration_us;
    float    rate_lpm;
    float    volume_l;
} SessionResult;

esp_err_t sensor_init(gpio_num_t pulse_gpio);

esp_err_t sensor_measure_session(SessionResult *out_result);
