#pragma once

#include "esp_err.h"
#include "soc/gpio_num.h"
#include "trichter_state.h"
#include <stdint.h>

// Animation style for a LED pattern
typedef enum {
  LED_EFFECT_OFF,    // dark
  LED_EFFECT_SOLID,  // constant colour
  LED_EFFECT_BLINK,  // on/off cycle with configurable timing
  LED_EFFECT_PULSE,  // smooth breathing (fade in / fade out)
} led_effect_t;

typedef struct {
  uint8_t r, g, b;
  led_effect_t effect;
  uint32_t on_ms;     // BLINK: on-phase duration in ms
  uint32_t off_ms;    // BLINK: off-phase duration in ms
  uint32_t period_ms; // PULSE: full breath cycle in ms
} led_pattern_t;

// Initialize the WS2812 LED driver on the given GPIO.
esp_err_t led_status_init(gpio_num_t gpio);

// Push a new pattern immediately, interrupting any current animation.
// Thread-safe — may be called from any FreeRTOS task.
void led_status_set(const led_pattern_t *pattern);

// Map an app_state_t to its configured pattern and apply it.
// Only sends an update when the state actually changes — cheap to call every
// main-loop tick.
void led_status_update_from_state(app_state_t state);
