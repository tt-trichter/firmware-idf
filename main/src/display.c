#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sensor.h"
#include <stdio.h>

#include "esp_lcd_panel_vendor.h"

static const char *TAG = "display";

#define I2C_HOST 0

#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (400 * 1000)
#define EXAMPLE_PIN_NUM_SDA GPIO_NUM_11
#define EXAMPLE_PIN_NUM_SCL GPIO_NUM_12
#define EXAMPLE_PIN_NUM_RST -1
#define EXAMPLE_I2C_HW_ADDR 0x3C

#define EXAMPLE_LCD_H_RES 128
#define EXAMPLE_LCD_V_RES 64

#define EXAMPLE_LCD_CMD_BITS 8
#define EXAMPLE_LCD_PARAM_BITS 8

lv_disp_t *display_init(void) {
  ESP_LOGI(TAG, "Initialize I2C bus");
  i2c_config_t i2c_conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = EXAMPLE_PIN_NUM_SDA,
      .scl_io_num = EXAMPLE_PIN_NUM_SCL,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
  };
  ESP_ERROR_CHECK(i2c_param_config(I2C_HOST, &i2c_conf));
  ESP_ERROR_CHECK(i2c_driver_install(I2C_HOST, I2C_MODE_MASTER, 0, 0, 0));

  ESP_LOGI(TAG, "Install panel IO");
  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_i2c_config_t io_config = {
      .dev_addr = EXAMPLE_I2C_HW_ADDR,
      .control_phase_bytes = 1,               // According to SSD1306 datasheet
      .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,   // According to SSD1306 datasheet
      .lcd_param_bits = EXAMPLE_LCD_CMD_BITS, // According to SSD1306 datasheet
      .dc_bit_offset = 6,                     // According to SSD1306 datasheet
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(I2C_HOST, &io_config, &io_handle));

  ESP_LOGI(TAG, "Install SSD1306 panel driver");
  esp_lcd_panel_handle_t panel_handle = NULL;
  esp_lcd_panel_dev_config_t panel_config = {
      .bits_per_pixel = 1,
      .reset_gpio_num = EXAMPLE_PIN_NUM_RST,
  };
  // This only work with esp-idf master:
  // esp_lcd_panel_ssd1306_config_t ssd1306_config = {
  //     .height = EXAMPLE_LCD_V_RES,
  // };
  // panel_config.vendor_config = &ssd1306_config;
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

  ESP_LOGI(TAG, "Initialize LVGL");
  const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  lvgl_port_init(&lvgl_cfg);

  const lvgl_port_display_cfg_t disp_cfg = {.io_handle = io_handle,
                                            .panel_handle = panel_handle,
                                            .buffer_size = EXAMPLE_LCD_H_RES *
                                                           EXAMPLE_LCD_V_RES,
                                            .double_buffer = true,
                                            .hres = EXAMPLE_LCD_H_RES,
                                            .vres = EXAMPLE_LCD_V_RES,
                                            .monochrome = true,
                                            .rotation = {
                                                .swap_xy = false,
                                                .mirror_x = false,
                                                .mirror_y = false,
                                            }};
  lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);

  /* Rotation of the screen */
  lv_disp_set_rotation(disp, LV_DISP_ROT_NONE);

  ESP_LOGI(TAG, "Display LVGL Scroll Text");

  return disp;
}

void display_write_await_session(lv_disp_t *disp) {
  lv_obj_t *scr = lv_disp_get_scr_act(disp);
  lv_obj_t *label = lv_label_create(scr);
  lv_label_set_long_mode(label,
                         LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
  lv_label_set_text(label, "Waiting for Session to finish...");
  /* Size of the screen (if you use rotation 90 or 270, please set
   * disp->driver->ver_res) */
  lv_obj_set_width(label, disp->driver->hor_res);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
}

void display_write_result(lv_disp_t *disp, const SessionResult *res) {
  lv_obj_t *scr = lv_disp_get_scr_act(disp);

  /*–– Duration label ––*/
  lv_obj_t *lbl_dur = lv_label_create(scr);
    ESP_LOGI(TAG, "Duration: %.2f", res->duration_us / 1e6f);
  lv_label_set_text_fmt(lbl_dur, "D: %.3f s", res->duration_us / 1e6f);
  lv_obj_set_width(lbl_dur, disp->driver->hor_res);
  lv_obj_align(lbl_dur, LV_ALIGN_TOP_MID, 0, 0);

  /*–– Flow-rate label ––*/
  lv_obj_t *lbl_rate = lv_label_create(scr);
  lv_label_set_long_mode(lbl_rate, LV_LABEL_LONG_WRAP);
    ESP_LOGI(TAG, "Rate: %.2f", res->rate_lpm);
  lv_label_set_text_fmt(lbl_rate, "R: %.2f L/min", res->rate_lpm);
  lv_obj_set_width(lbl_rate, disp->driver->hor_res);
  lv_obj_align(lbl_rate, LV_ALIGN_TOP_MID, 0, 20);

  /*–– Volume label ––*/
  lv_obj_t *lbl_vol = lv_label_create(scr);
  lv_label_set_long_mode(lbl_vol, LV_LABEL_LONG_WRAP);
    ESP_LOGI(TAG, "Voli,e: %.2f", res->volume_l);
  lv_label_set_text_fmt(lbl_vol, "V: %.2f L", res->volume_l);
  lv_obj_set_width(lbl_vol, disp->driver->hor_res);
  lv_obj_align(lbl_vol, LV_ALIGN_TOP_MID, 0, 40);
}
