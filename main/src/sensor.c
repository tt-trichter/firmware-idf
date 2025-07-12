#include "sensor.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "hal/pcnt_types.h"
#include "sdkconfig.h"
#include <stdio.h>

static const char *TAG = "sensor_driver";

static pcnt_unit_handle_t s_pcnt_unit = NULL;
static pcnt_channel_handle_t s_pcnt_chan = NULL;
static SemaphoreHandle_t s_first_sem = NULL;
static SemaphoreHandle_t s_idle_sem = NULL;
static esp_timer_handle_t s_idle_timer = NULL;
static volatile uint64_t s_first_ts = 0;

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
  s_first_sem = xSemaphoreCreateBinary();
  s_idle_sem = xSemaphoreCreateBinary();
  if (!s_first_sem || !s_idle_sem) {
    ESP_LOGE(TAG, "Failed to create semaphores");
    return ESP_ERR_NO_MEM;
  }

  const esp_timer_create_args_t idle_args = {
      .callback = idle_timer_cb, .arg = NULL, .name = "idle_timer"};
  ESP_ERROR_CHECK(esp_timer_create(&idle_args, &s_idle_timer));

  gpio_config_t io_conf = {.pin_bit_mask = 1ULL << pulse_gpio,
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_POSEDGE};
  ESP_ERROR_CHECK(gpio_config(&io_conf));
  ESP_ERROR_CHECK(gpio_install_isr_service(0));
  ESP_ERROR_CHECK(
      gpio_isr_handler_add(pulse_gpio, gpio_first_isr, (void *)pulse_gpio));

  pcnt_unit_config_t unit_cfg = {.low_limit = -1,
                                 .high_limit = INT16_MAX,
                                 .intr_priority = 1,
                                 .flags.accum_count = 1};
  ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_pcnt_unit));

  pcnt_glitch_filter_config_t filt = {.max_glitch_ns = CONFIG_SENSOR_GLITCH_NS};
  ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_pcnt_unit, &filt));

  pcnt_chan_config_t chan_cfg = {
      .edge_gpio_num = pulse_gpio, .level_gpio_num = -1, .flags = {0}};
  ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt_unit, &chan_cfg, &s_pcnt_chan));
  ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
      s_pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
      PCNT_CHANNEL_LEVEL_ACTION_KEEP));

  ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt_unit));

  ESP_LOGI(TAG, "Driver init: GPIO=%d, startup=%dms, idle=%dms, glitch=%dns",
           pulse_gpio, CONFIG_SENSOR_STARTUP_WINDOW_MS,
           CONFIG_SENSOR_IDLE_TIMEOUT_MS, CONFIG_SENSOR_GLITCH_NS);
  return ESP_OK;
}

esp_err_t sensor_measure_session(SessionResult *out_result) {
  xSemaphoreTake(s_first_sem, 0);
  xSemaphoreTake(s_idle_sem, 0);

  ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt_unit));
  ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt_unit));

  if (xSemaphoreTake(s_first_sem, portMAX_DELAY) != pdTRUE) {
    ESP_LOGE(TAG, "Timeout waiting first pulse");
    return ESP_FAIL;
  }
  uint64_t t_start = s_first_ts;
  uint64_t startup_deadline =
      t_start + (uint64_t)CONFIG_SENSOR_STARTUP_WINDOW_MS * 1000ULL;

  int startup_count = 1;
  while (esp_timer_get_time() < startup_deadline) {
    int cnt;
    ESP_ERROR_CHECK(pcnt_unit_get_count(s_pcnt_unit, &cnt));
    startup_count = cnt;
  }
  if (startup_count < CONFIG_SENSOR_STARTUP_PULSES) {
    ESP_LOGW(TAG, "Startup window %dms: only %d pulses",
             CONFIG_SENSOR_STARTUP_WINDOW_MS, startup_count);
    ESP_ERROR_CHECK(pcnt_unit_stop(s_pcnt_unit));
    return ESP_ERR_TIMEOUT;
  }

  esp_timer_start_once(s_idle_timer, CONFIG_SENSOR_IDLE_TIMEOUT_MS * 1000ULL);
  int last_count = startup_count;

  while (true) {
    int cnt;
    ESP_ERROR_CHECK(pcnt_unit_get_count(s_pcnt_unit, &cnt));
    ESP_LOGI(TAG, "COUNT: %d, LAST_COUNT: %d", cnt, last_count);
    if (cnt != last_count) {
      last_count = cnt;
      esp_timer_stop(s_idle_timer);
      esp_timer_start_once(s_idle_timer,
                           CONFIG_SENSOR_IDLE_TIMEOUT_MS * 1000ULL);
    }
    if (xSemaphoreTake(s_idle_sem, 0) == pdTRUE) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  uint64_t t_end = esp_timer_get_time();

  ESP_ERROR_CHECK(pcnt_unit_stop(s_pcnt_unit));
  int total_pulses;
  ESP_ERROR_CHECK(pcnt_unit_get_count(s_pcnt_unit, &total_pulses));

  uint64_t dur_us = t_end - t_start;
  float secs = dur_us / 1e6f;
  float rate_lpm = (total_pulses / secs) / PULSES_PER_LITER;
  float volume_l = rate_lpm * (secs / 60.0f);

  out_result->duration_us = dur_us;
  out_result->rate_lpm = rate_lpm;
  out_result->volume_l = volume_l;

  ESP_LOGI(TAG, "Result: %d pulses, %.2fs, %.2f L/min, %.2f L", total_pulses,
           secs, rate_lpm, volume_l);
  return ESP_OK;
}
