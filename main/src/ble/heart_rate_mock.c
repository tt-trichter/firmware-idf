#include "ble/ble.h"
#include "ble/heart_rate.h"

static const char *TAG = "ble_heart_rate";

static uint8_t heart_rate;

uint8_t get_heart_rate(void) { return heart_rate; }

void update_heart_rate(void) { heart_rate = 60 + (uint8_t)(esp_random() % 21); }
