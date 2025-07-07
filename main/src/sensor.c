#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "hal/pcnt_types.h"
#include "sdkconfig.h"
#include "sensor.h"
#include <stdio.h>

static const char* TAG = "sensor_driver";

static pcnt_unit_handle_t    s_unit         = NULL;
static pcnt_channel_handle_t s_chan         = NULL;
static SemaphoreHandle_t     s_startup_sem  = NULL;
static SemaphoreHandle_t     s_idle_sem     = NULL;
static esp_timer_handle_t    s_idle_timer   = NULL;
static uint64_t              s_startup_ts   = 0;

static bool IRAM_ATTR pcnt_startup_cb(pcnt_unit_handle_t unit,
                                      const pcnt_watch_event_data_t *edata,
                                      void *user_ctx)
{
    if (edata->watch_point_value == CONFIG_SENSOR_STARTUP_PULSES) {
        s_startup_ts = esp_timer_get_time();
        BaseType_t hpw = pdFALSE;
        xSemaphoreGiveFromISR(s_startup_sem, &hpw);
        if (hpw) portYIELD_FROM_ISR();
    }
    return true;
}

static void IRAM_ATTR idle_cb(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_idle_sem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t sensor_init(gpio_num_t pulse_gpio)
{
    s_startup_sem = xSemaphoreCreateBinary();
    s_idle_sem    = xSemaphoreCreateBinary();
    if (!s_startup_sem || !s_idle_sem) {
        ESP_LOGE(TAG, "Failed to create semaphores");
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t idle_args = {
        .callback = idle_cb,
        .arg      = NULL,
        .name     = "idle_timer",
    };
    ESP_ERROR_CHECK(esp_timer_create(&idle_args, &s_idle_timer));

    pcnt_unit_config_t unit_cfg = {
        .low_limit     = -1,
        .high_limit    = CONFIG_SENSOR_STARTUP_PULSES + 1,
        .intr_priority = 1,
        .flags.accum_count = 1,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_unit));

    pcnt_glitch_filter_config_t filt = {
        .max_glitch_ns = CONFIG_SENSOR_GLITCH_NS,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_unit, &filt));

    // PCNT channel
    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num  = pulse_gpio,
        .level_gpio_num = -1,
        .flags = {0},
    };
    ESP_ERROR_CHECK(pcnt_new_channel(s_unit, &chan_cfg, &s_chan));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
        s_chan,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_HOLD
    ));

    pcnt_event_callbacks_t cbs = {.on_reach = pcnt_startup_cb};
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(s_unit, &cbs, NULL));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_unit, CONFIG_SENSOR_STARTUP_PULSES));

    ESP_ERROR_CHECK(pcnt_unit_enable(s_unit));

    ESP_LOGI(TAG,
             "PCNT init: gpio=%d, startup=%dpulses in %dms, idle=%dms, glitch=%dns",
             pulse_gpio,
             CONFIG_SENSOR_STARTUP_PULSES,
             CONFIG_SENSOR_STARTUP_WINDOW_MS,
             CONFIG_SENSOR_IDLE_TIMEOUT_MS,
             CONFIG_SENSOR_GLITCH_NS);
    return ESP_OK;
}

esp_err_t sensor_measure_session(SessionResult *out_result)
{
    xSemaphoreTake(s_startup_sem, 0);
    xSemaphoreTake(s_idle_sem, 0);

    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(s_unit));

    if (xSemaphoreTake(s_startup_sem,
        pdMS_TO_TICKS(CONFIG_SENSOR_STARTUP_WINDOW_MS)) != pdTRUE) {
        int failed_pulses = 0;
        ESP_ERROR_CHECK(pcnt_unit_get_count(s_unit, &failed_pulses));
        ESP_LOGW(TAG, "Startup window timeout (%d ms): only %d pulses",
                 CONFIG_SENSOR_STARTUP_WINDOW_MS,
                 failed_pulses);
        ESP_ERROR_CHECK(pcnt_unit_stop(s_unit));
        return ESP_ERR_TIMEOUT;
    }
    uint64_t t0 = s_startup_ts;

    esp_timer_start_once(s_idle_timer,
        (uint64_t)CONFIG_SENSOR_IDLE_TIMEOUT_MS * 1000ULL);
    int last_count = 0;
    ESP_ERROR_CHECK(pcnt_unit_get_count(s_unit, &last_count));

    // Poll counter, reset timer on new pulses
    while (true) {
        int count;
        ESP_ERROR_CHECK(pcnt_unit_get_count(s_unit, &count));
        if (count != last_count) {
            last_count = count;
            esp_timer_stop(s_idle_timer);
            esp_timer_start_once(s_idle_timer,
                (uint64_t)CONFIG_SENSOR_IDLE_TIMEOUT_MS * 1000ULL);
        }
        // Check if idle timeout occurred
        if (xSemaphoreTake(s_idle_sem, 0) == pdTRUE) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    uint64_t t1 = esp_timer_get_time();

    // Stop and finalize
    ESP_ERROR_CHECK(pcnt_unit_stop(s_unit));
    int pulses = last_count;

    uint64_t duration_us = t1 - t0;
    float seconds       = duration_us / 1e6f;
    float rate_lpm      = (pulses / seconds) * 60.0f / PULSES_PER_LITER;
    float volume_l      = rate_lpm * (seconds / 60.0f);

    out_result->duration_us = duration_us;
    out_result->rate_lpm    = rate_lpm;
    out_result->volume_l    = volume_l;

    ESP_LOGI(TAG,
             "Measured: %d pulses, %.2f s, %.2f L/min, %.2f L",
             pulses, seconds, rate_lpm, volume_l);

    return ESP_OK;
}
