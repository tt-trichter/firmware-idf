#include "led_status/led_status.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "trichter_config.h"

static const char *TAG = "led_status";

// ─── State → pattern table ────────────────────────────────────────────────────
// To change how a state looks, edit the corresponding row.
// Brightness is intentionally low (out of 255) so the LED is not blinding.
// Fields: { r,   g,   b,   effect,           on_ms, off_ms, period_ms }
static const led_pattern_t k_state_patterns[] = {
    [APP_STATE_INIT] =
        {20, 20, 20, LED_EFFECT_PULSE, 0, 0, 1500},  // white breathing
    [APP_STATE_READY] =
        {0, 30, 0, LED_EFFECT_SOLID, 0, 0, 0},        // solid green
    [APP_STATE_WAITING_SESSION] =
        {0, 0, 50, LED_EFFECT_BLINK, 800, 400, 0},    // blue slow blink
    [APP_STATE_SESSION_RUNNING] =
        {0, 60, 0, LED_EFFECT_BLINK, 150, 150, 0},    // green fast blink
    [APP_STATE_SESSION_COMPLETE] =
        {0, 50, 0, LED_EFFECT_SOLID, 0, 0, 0},        // solid green
    [APP_STATE_TRANSFERRING_IMAGE] =
        {0, 40, 40, LED_EFFECT_BLINK, 100, 100, 0},   // cyan fast blink
    [APP_STATE_ERROR] =
        {60, 0, 0, LED_EFFECT_BLINK, 200, 200, 0},    // red fast blink
};
// ─────────────────────────────────────────────────────────────────────────────

#define PULSE_STEPS 32  // fade steps per half-period (fade-in or fade-out)

static led_strip_handle_t s_strip     = NULL;
static QueueHandle_t      s_queue     = NULL;
static app_state_t        s_last_state = (app_state_t)-1; // force first update

static void set_pixel(uint8_t r, uint8_t g, uint8_t b) {
  led_strip_set_pixel(s_strip, 0, r, g, b);
  led_strip_refresh(s_strip);
}

static void led_task(void *arg) {
  led_pattern_t p = {.effect = LED_EFFECT_OFF};
  set_pixel(0, 0, 0);

  while (true) {
    switch (p.effect) {

    case LED_EFFECT_OFF:
      set_pixel(0, 0, 0);
      xQueueReceive(s_queue, &p, portMAX_DELAY);
      break;

    case LED_EFFECT_SOLID:
      set_pixel(p.r, p.g, p.b);
      xQueueReceive(s_queue, &p, portMAX_DELAY);
      break;

    case LED_EFFECT_BLINK: {
      // On phase — break early if a new pattern arrives
      led_pattern_t next;
      set_pixel(p.r, p.g, p.b);
      if (xQueueReceive(s_queue, &next, pdMS_TO_TICKS(p.on_ms)) == pdTRUE) {
        p = next;
        break;
      }
      // Off phase — break early if a new pattern arrives
      set_pixel(0, 0, 0);
      if (xQueueReceive(s_queue, &next, pdMS_TO_TICKS(p.off_ms)) == pdTRUE) {
        p = next;
      }
      break;
    }

    case LED_EFFECT_PULSE: {
      uint32_t step_ms = p.period_ms / (2 * PULSE_STEPS);
      if (step_ms == 0) step_ms = 1;
      bool interrupted = false;

      // Fade in: 0 → full brightness
      for (int i = 0; i <= PULSE_STEPS && !interrupted; i++) {
        set_pixel(p.r * i / PULSE_STEPS, p.g * i / PULSE_STEPS,
                  p.b * i / PULSE_STEPS);
        led_pattern_t next;
        if (xQueueReceive(s_queue, &next, pdMS_TO_TICKS(step_ms)) == pdTRUE) {
          p = next;
          interrupted = true;
        }
      }
      // Fade out: full brightness → 0
      for (int i = PULSE_STEPS; i >= 0 && !interrupted; i--) {
        set_pixel(p.r * i / PULSE_STEPS, p.g * i / PULSE_STEPS,
                  p.b * i / PULSE_STEPS);
        led_pattern_t next;
        if (xQueueReceive(s_queue, &next, pdMS_TO_TICKS(step_ms)) == pdTRUE) {
          p = next;
          interrupted = true;
        }
      }
      break;
    }

    }  // switch
  }    // while
}

esp_err_t led_status_init(gpio_num_t gpio) {
  led_strip_config_t strip_cfg = {
      .strip_gpio_num = gpio,
      .max_leds       = 1,
      .led_model      = LED_MODEL_WS2812,
  };
  led_strip_rmt_config_t rmt_cfg = {
      .resolution_hz = 10 * 1000 * 1000,  // 10 MHz — suits WS2812 timing
  };

  esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
  if (err != ESP_OK) {
    TRICHTER_LOGE(TAG, "Failed to create LED strip on GPIO %d: %s", gpio,
                  esp_err_to_name(err));
    return err;
  }
  led_strip_clear(s_strip);

  s_queue = xQueueCreate(1, sizeof(led_pattern_t));
  if (!s_queue) {
    TRICHTER_LOGE(TAG, "Failed to create LED pattern queue");
    return ESP_ERR_NO_MEM;
  }

  BaseType_t rc =
      xTaskCreate(led_task, "led_status", 2 * 1024, NULL, 3, NULL);
  if (rc != pdPASS) {
    TRICHTER_LOGE(TAG, "Failed to create LED task");
    return ESP_FAIL;
  }

  TRICHTER_LOGI(TAG, "LED status driver initialized on GPIO %d", gpio);
  return ESP_OK;
}

void led_status_set(const led_pattern_t *pattern) {
  if (!s_queue || !pattern) {
    return;
  }
  xQueueOverwrite(s_queue, pattern);
}

void led_status_update_from_state(app_state_t state) {
  if (state == s_last_state) {
    return;
  }
  s_last_state = state;

  size_t n = sizeof(k_state_patterns) / sizeof(k_state_patterns[0]);
  if ((size_t)state >= n) {
    TRICHTER_LOGW(TAG, "No LED pattern for state %d — LED off", state);
    static const led_pattern_t off = {.effect = LED_EFFECT_OFF};
    led_status_set(&off);
    return;
  }

  TRICHTER_LOGI(TAG, "LED pattern update for state %d", (int)state);
  led_status_set(&k_state_patterns[state]);
}
