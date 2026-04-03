#pragma once

#include "esp_random.h"
#include <stdint.h>

void trichter_ble_security_init(void);

void trichter_ble_show_passkey(uint32_t passkey);

void trichter_ble_on_pairing_complete(int status);

static uint32_t trichter_generate_passkey() {
  return (uint32_t)(esp_random() % 1000000);
}
