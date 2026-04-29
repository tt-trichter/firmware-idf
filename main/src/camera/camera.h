#pragma once

#include "sdkconfig.h"

#ifdef CONFIG_ENABLE_CAMERA
#define PWDN_GPIO_NUM  CONFIG_CAMERA_PIN_PWDN
#define RESET_GPIO_NUM CONFIG_CAMERA_PIN_RESET
#define XCLK_GPIO_NUM  CONFIG_CAMERA_PIN_XCLK
#define SIOD_GPIO_NUM  CONFIG_CAMERA_PIN_SIOD
#define SIOC_GPIO_NUM  CONFIG_CAMERA_PIN_SIOC

#define Y9_GPIO_NUM    CONFIG_CAMERA_PIN_Y9
#define Y8_GPIO_NUM    CONFIG_CAMERA_PIN_Y8
#define Y7_GPIO_NUM    CONFIG_CAMERA_PIN_Y7
#define Y6_GPIO_NUM    CONFIG_CAMERA_PIN_Y6
#define Y5_GPIO_NUM    CONFIG_CAMERA_PIN_Y5
#define Y4_GPIO_NUM    CONFIG_CAMERA_PIN_Y4
#define Y3_GPIO_NUM    CONFIG_CAMERA_PIN_Y3
#define Y2_GPIO_NUM    CONFIG_CAMERA_PIN_Y2
#define VSYNC_GPIO_NUM CONFIG_CAMERA_PIN_VSYNC
#define HREF_GPIO_NUM  CONFIG_CAMERA_PIN_HREF
#define PCLK_GPIO_NUM  CONFIG_CAMERA_PIN_PCLK

#define LED_GPIO_NUM   CONFIG_CAMERA_PIN_LED
#endif

#include "esp_camera.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "trichter_error.h"

esp_err_t camera_init_module(void);

camera_fb_t *camera_capture_frame(void);

void camera_clear_fb(void);
