#pragma once

#include "esp_random.h"

#define HEART_RATE_TASK_PERIOD (1000 / portTICK_PERIOD_MS)

uint8_t get_heart_rate(void);
void update_heart_rate(void);

