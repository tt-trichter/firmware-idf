#include "sensor/sensor.h"
#if CONFIG_ENABLE_CAMERA
#include "camera/camera.h"
#endif
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "hal/pcnt_types.h"
#include "sdkconfig.h"
#include "trichter_error.h"
#include <inttypes.h>
#include <stdio.h>

static const char *TAG = "sensor_driver";

static pcnt_unit_handle_t s_pcnt_unit = NULL;
static pcnt_channel_handle_t s_pcnt_chan = NULL;
static SemaphoreHandle_t s_first_sem = NULL;
static SemaphoreHandle_t s_idle_sem = NULL;
static esp_timer_handle_t s_idle_timer = NULL;
static volatile uint64_t s_first_ts = 0;
static gpio_num_t s_pulse_gpio = GPIO_NUM_NC;
static bool s_armed = false;

#define PULSES_PER_LITER 6.6f

static void IRAM_ATTR gpio_first_isr(void *arg) {
  BaseType_t hpw = pdFALSE;
  s_first_ts = esp_timer_get_time();
  xSemaphoreGiveFromISR(s_first_sem, &hpw);
  gpio_intr_disable((gpio_num_t)arg);
  if (hpw)
    portYIELD_FROM_ISR();
}

static void IRAM_ATTR idle_timer_cb(void *arg) {
  BaseType_t hpw = pdFALSE;
  xSemaphoreGiveFromISR(s_idle_sem, &hpw);
  if (hpw)
    portYIELD_FROM_ISR();
}

esp_err_t sensor_init(gpio_num_t pulse_gpio) {
  s_pulse_gpio = pulse_gpio;
  s_first_sem = xSemaphoreCreateBinary();
  s_idle_sem = xSemaphoreCreateBinary();
  TRICHTER_CHECK(s_first_sem && s_idle_sem, TRICHTER_ERR_MEMORY,
                 "Failed to create semaphores");

  const esp_timer_create_args_t idle_args = {
      .callback = idle_timer_cb, .arg = NULL, .name = "idle_timer"};
  TRICHTER_CHECK_ERR(esp_timer_create(&idle_args, &s_idle_timer),
                     TRICHTER_ERR_INIT, "Failed to create idle timer");

  gpio_config_t io_conf = {.pin_bit_mask = 1ULL << pulse_gpio,
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_POSEDGE};
  TRICHTER_CHECK_ERR(gpio_config(&io_conf), TRICHTER_ERR_SENSOR,
                     "Failed to configure GPIO");
  TRICHTER_CHECK_ERR(gpio_install_isr_service(0), TRICHTER_ERR_SENSOR,
                     "Failed to install GPIO ISR service");
  TRICHTER_CHECK_ERR(
      gpio_isr_handler_add(pulse_gpio, gpio_first_isr, (void *)pulse_gpio),
      TRICHTER_ERR_SENSOR, "Failed to add GPIO ISR handler");

  pcnt_unit_config_t unit_cfg = {.low_limit = -1,
                                 .high_limit = INT16_MAX,
                                 .intr_priority = 1,
                                 .flags.accum_count = 1};
  TRICHTER_CHECK_ERR(pcnt_new_unit(&unit_cfg, &s_pcnt_unit),
                     TRICHTER_ERR_SENSOR, "Failed to create PCNT unit");

  pcnt_glitch_filter_config_t filt = {.max_glitch_ns = CONFIG_SENSOR_GLITCH_NS};
  TRICHTER_CHECK_ERR(pcnt_unit_set_glitch_filter(s_pcnt_unit, &filt),
                     TRICHTER_ERR_SENSOR, "Failed to set glitch filter");

  pcnt_chan_config_t chan_cfg = {
      .edge_gpio_num = pulse_gpio, .level_gpio_num = -1, .flags = {0}};
  TRICHTER_CHECK_ERR(pcnt_new_channel(s_pcnt_unit, &chan_cfg, &s_pcnt_chan),
                     TRICHTER_ERR_SENSOR, "Failed to create PCNT channel");
  TRICHTER_CHECK_ERR(pcnt_channel_set_edge_action(
                         s_pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                         PCNT_CHANNEL_LEVEL_ACTION_KEEP),
                     TRICHTER_ERR_SENSOR, "Failed to set PCNT edge action");

  TRICHTER_CHECK_ERR(pcnt_unit_enable(s_pcnt_unit), TRICHTER_ERR_SENSOR,
                     "Failed to enable PCNT unit");

  TRICHTER_LOGI(TAG,
                "Driver init: GPIO=%d, startup=%dms, idle=%dms, glitch=%dns",
                pulse_gpio, CONFIG_SENSOR_STARTUP_WINDOW_MS,
                CONFIG_SENSOR_IDLE_TIMEOUT_MS, CONFIG_SENSOR_GLITCH_NS);
  return ESP_OK;
}

esp_err_t sensor_arm(void) {
  if (s_armed) {
    return ESP_OK;
  }

  // Drain any leftover semaphore tokens from the previous session
  xSemaphoreTake(s_first_sem, 0);
  xSemaphoreTake(s_idle_sem, 0);

  TRICHTER_CHECK_ERR(pcnt_unit_clear_count(s_pcnt_unit), TRICHTER_ERR_SENSOR,
                     "Failed to clear PCNT count");
  TRICHTER_CHECK_ERR(pcnt_unit_start(s_pcnt_unit), TRICHTER_ERR_SENSOR,
                     "Failed to start PCNT unit");

  gpio_intr_enable(s_pulse_gpio);
  s_armed = true;
  TRICHTER_LOGI(TAG, "Sensor armed — waiting for first pulse on GPIO %d",
                s_pulse_gpio);
  return ESP_OK;
}

bool sensor_poll_triggered(void) {
  if (!s_armed) {
    return false;
  }
  if (xSemaphoreTake(s_first_sem, 0) == pdTRUE) {
    s_armed = false;
    TRICHTER_LOGI(TAG, "First pulse detected at t=%" PRIu64 " us", s_first_ts);
    return true;
  }
  return false;
}

esp_err_t sensor_measure_session(SessionResult *out_result) {
  uint64_t t_start = s_first_ts;
  uint64_t startup_deadline =
      t_start + (uint64_t)CONFIG_SENSOR_STARTUP_WINDOW_MS * 1000ULL;

  uint64_t now = esp_timer_get_time();
  int64_t remaining_us = (int64_t)(startup_deadline - now);
  TRICHTER_LOGI(TAG,
                "Startup validation: window=%dms, required=%d pulses, "
                "%" PRId64 "ms remaining in window",
                CONFIG_SENSOR_STARTUP_WINDOW_MS, CONFIG_SENSOR_STARTUP_PULSES,
                remaining_us / 1000);

  // Wait out the remainder of the startup window, yielding to IDLE each tick.
  int startup_count = 1;
  while (esp_timer_get_time() < startup_deadline) {
    vTaskDelay(1); // 1 tick — always yields; PCNT counts in hardware
    int cnt;
    TRICHTER_CHECK_ERR(pcnt_unit_get_count(s_pcnt_unit, &cnt),
                       TRICHTER_ERR_SENSOR,
                       "Failed to get PCNT count during startup");
    startup_count = cnt;
  }
  if (startup_count < CONFIG_SENSOR_STARTUP_PULSES) {
    TRICHTER_LOGW(TAG,
                  "Startup gate FAILED: %d pulses in %dms window (need %d) — "
                  "rejecting session, re-arming",
                  startup_count, CONFIG_SENSOR_STARTUP_WINDOW_MS,
                  CONFIG_SENSOR_STARTUP_PULSES);
    TRICHTER_CHECK_ERR(pcnt_unit_stop(s_pcnt_unit), TRICHTER_ERR_SENSOR,
                       "Failed to stop PCNT unit after startup timeout");
    TRICHTER_LOG_ERROR(TRICHTER_ERR_TIMEOUT, ESP_ERR_TIMEOUT,
                       "Insufficient pulses during startup window");
    return ESP_ERR_TIMEOUT;
  }

  TRICHTER_LOGI(TAG, "Startup gate PASSED: %d pulses — session running",
                startup_count);

  // Arm idle timer immediately after startup gate so stop is always detected
  esp_timer_start_once(s_idle_timer, CONFIG_SENSOR_IDLE_TIMEOUT_MS * 1000ULL);
  int last_count = startup_count;

#if CONFIG_ENABLE_CAMERA
  camera_fb_t *session_image = NULL;
  bool photo_taken = false;
  uint64_t t_photo = t_start + (uint64_t)CONFIG_CAMERA_CAPTURE_DELAY_MS * 1000ULL;
#endif

  // Poll interval: short enough to restart the idle timer promptly on new
  // pulses, long enough to yield to IDLE and avoid the task watchdog.
  // pdMS_TO_TICKS(1) == 0 at the default 100 Hz tick rate, which turns
  // vTaskDelay into a no-yield busy spin — use a real tick interval instead.
  const TickType_t POLL_TICKS = pdMS_TO_TICKS(10);

  while (true) {
    // Block until the idle timer fires OR the poll interval elapses.
    // This is the only blocking call in the loop, so IDLE always gets CPU.
    if (xSemaphoreTake(s_idle_sem, POLL_TICKS) == pdTRUE) {
      break;
    }

    int cnt;
    TRICHTER_CHECK_ERR(pcnt_unit_get_count(s_pcnt_unit, &cnt),
                       TRICHTER_ERR_SENSOR,
                       "Failed to get PCNT count during session");
    TRICHTER_LOGD(TAG, "COUNT: %d, LAST_COUNT: %d", cnt, last_count);
    if (cnt != last_count) {
      last_count = cnt;
      esp_timer_stop(s_idle_timer);
      esp_timer_start_once(s_idle_timer,
                           CONFIG_SENSOR_IDLE_TIMEOUT_MS * 1000ULL);
    }

#if CONFIG_ENABLE_CAMERA
    if (!photo_taken && esp_timer_get_time() >= t_photo) {
      photo_taken = true;
      TRICHTER_LOGI(TAG, "Capturing image during session...");
      session_image = camera_capture_frame();
      if (!session_image) {
        TRICHTER_LOGW(TAG, "Failed to capture image during session");
      }
    }
#endif
  }
  uint64_t t_end = esp_timer_get_time();

  TRICHTER_CHECK_ERR(pcnt_unit_stop(s_pcnt_unit), TRICHTER_ERR_SENSOR,
                     "Failed to stop PCNT unit");
  int total_pulses;
  TRICHTER_CHECK_ERR(pcnt_unit_get_count(s_pcnt_unit, &total_pulses),
                     TRICHTER_ERR_SENSOR, "Failed to get final PCNT count");

  uint64_t dur_us = t_end - t_start;
  float secs = dur_us / 1e6f;
  float rate_lpm = (total_pulses / secs) / PULSES_PER_LITER;
  float volume_l = rate_lpm * (secs / 60.0f);

  out_result->duration_us = dur_us;
  out_result->rate_lpm = rate_lpm;
  out_result->volume_l = volume_l;
#if CONFIG_ENABLE_CAMERA
  out_result->image_fb = session_image;
  TRICHTER_LOGI(TAG, "Result: %d pulses, %.2fs, %.2f L/min, %.2f L, image: %s",
                total_pulses, secs, rate_lpm, volume_l,
                session_image ? "captured" : "none");
#else
  out_result->image_fb = NULL;
  TRICHTER_LOGI(TAG, "Result: %d pulses, %.2fs, %.2f L/min, %.2f L",
                total_pulses, secs, rate_lpm, volume_l);
#endif
  return ESP_OK;
}

void sensor_cleanup_session_result(SessionResult *result) {
  if (result && result->image_fb) {
    esp_camera_fb_return(result->image_fb);
    result->image_fb = NULL;
    TRICHTER_LOGD(TAG, "Session result image buffer released");
  }
}
