#include "driver/i2c_master.h"
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
#include "img_icon.h"
#include <stdio.h>

#include "esp_lcd_panel_vendor.h"

static const char *TAG = "display";

#define I2C_MASTER_NUM I2C_NUM_0
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (400 * 1000)
#define EXAMPLE_PIN_NUM_SDA GPIO_NUM_5
#define EXAMPLE_PIN_NUM_SCL GPIO_NUM_6
#define EXAMPLE_PIN_NUM_RST -1
#define EXAMPLE_I2C_HW_ADDR 0x3C

#define EXAMPLE_LCD_H_RES 128
#define EXAMPLE_LCD_V_RES 64

#define EXAMPLE_LCD_CMD_BITS 8
#define EXAMPLE_LCD_PARAM_BITS 8

static i2c_master_bus_handle_t i2c_bus_handle = NULL;

lv_disp_t *display_init(void)
{
#ifdef CONFIG_ENABLE_DISPLAY
  ESP_LOGI(TAG, "Initialize I2C master bus");
  i2c_master_bus_config_t bus_config = {
      .i2c_port = I2C_MASTER_NUM,
      .sda_io_num = EXAMPLE_PIN_NUM_SDA,
      .scl_io_num = EXAMPLE_PIN_NUM_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus_handle));

  ESP_LOGI(TAG, "Install panel IO");
  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_i2c_config_t io_config = {
      .dev_addr = EXAMPLE_I2C_HW_ADDR,
      .scl_speed_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
      .control_phase_bytes = 1,               // According to SSD1306 datasheet
      .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,   // According to SSD1306 datasheet
      .lcd_param_bits = EXAMPLE_LCD_CMD_BITS, // According to SSD1306 datasheet
      .dc_bit_offset = 6,                     // According to SSD1306 datasheet
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_handle, &io_config, &io_handle));

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
#else
  {
    ESP_LOGW(TAG, "Display module is disabled in configuration");
    return NULL;
  }
#endif
}

void display_write_await_session(lv_disp_t *disp)
{
#ifdef CONFIG_ENABLE_DISPLAY
  lv_obj_t *scr = lv_disp_get_scr_act(disp);
  lv_obj_clean(scr);

  lv_obj_t *label = lv_label_create(scr);
  lv_obj_t *img = lv_img_create(scr);

  lv_img_set_src(img, &img_icon);
  lv_obj_align(img, LV_ALIGN_TOP_MID, 0, -5);

  ESP_LOGI(TAG, "Icon displayed on screen");

  lv_label_set_long_mode(label,
                         LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
  lv_label_set_text(label, "Waiting for Session...");
  lv_obj_set_width(label, disp->driver->hor_res);
  lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);
#else
  ESP_LOGW(TAG, "Display module is disabled in configuration, skipping display write");
#endif
}

void display_write_result(lv_disp_t *disp, const SessionResult *res)
{
#ifdef CONFIG_ENABLE_DISPLAY
  lv_obj_t *scr = lv_disp_get_scr_act(disp);
  lv_obj_clean(scr);

  lv_obj_t *lbl_dur = lv_label_create(scr);
  lv_label_set_text_fmt(lbl_dur, "%.2f L in %.2f s!", res->volume_l, res->duration_us / 1e6f);
  lv_obj_set_width(lbl_dur, disp->driver->hor_res);
  lv_obj_align(lbl_dur, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t *lbl_rate = lv_label_create(scr);
  lv_label_set_long_mode(lbl_rate, LV_LABEL_LONG_WRAP);
  ESP_LOGI(TAG, "Rate: %.2f", res->rate_lpm);
  lv_label_set_text_fmt(lbl_rate, "-> %.2f L/min", res->rate_lpm);
  lv_obj_set_width(lbl_rate, disp->driver->hor_res);
  lv_obj_align(lbl_rate, LV_ALIGN_CENTER, 0, 10);

#else
  ESP_LOGW(TAG, "Display module is disabled in configuration, skipping display write");
#endif
}

void display_show_icon(lv_disp_t *disp)
{
#ifdef CONFIG_ENABLE_DISPLAY
#else
  ESP_LOGW(TAG, "Display module is disabled in configuration, skipping icon display");
#endif
}

void display_cleanup(void)
{
  if (i2c_bus_handle != NULL)
  {
    ESP_LOGI(TAG, "Deinitialize I2C master bus");
    ESP_ERROR_CHECK(i2c_del_master_bus(i2c_bus_handle));
    i2c_bus_handle = NULL;
  }
}
