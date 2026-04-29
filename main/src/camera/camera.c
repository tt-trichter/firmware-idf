#include "camera/camera.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "trichter_error.h"

static const char *TAG = "camera";

#ifdef CONFIG_ENABLE_CAMERA
static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sccb_sda = SIOD_GPIO_NUM,
    .pin_sccb_scl = SIOC_GPIO_NUM,
    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,
    .xclk_freq_hz = 20000000,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_VGA, // 720x1280 - portrait mode
    .jpeg_quality = 15,
    .fb_count = 2,
    .grab_mode = CAMERA_GRAB_LATEST};
#else
static camera_config_t camera_config = {};
#endif

esp_err_t camera_init_module(void) {
#ifdef CONFIG_ENABLE_CAMERA
  esp_err_t err = esp_camera_init(&camera_config);
  TRICHTER_CHECK_ERR(err, TRICHTER_ERR_CAMERA, "Camera initialization failed");

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);

    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);

    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_ae_level(s, 1);

    s->set_special_effect(s, 0);
  }

  for (int i = 0; i < 5; ++i) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  TRICHTER_LOGI(TAG, "Camera initialized successfully");
  return ESP_OK;
#else
  TRICHTER_LOGW(TAG, "Camera module is disabled in configuration");
  return ESP_OK;
#endif
}

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

static size_t jpg_encode_stream(void *arg, size_t index, const void *data,
                                size_t len) {
  jpg_chunking_t *j = arg;
  if (!index) {
    j->len = 0;
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
    return 0;
  }
  j->len += len;
  return len;
}

camera_fb_t *camera_capture_frame(void) {
#ifdef CONFIG_ENABLE_CAMERA
  for (int i = 0; i < 5; ++i) {
    camera_fb_t *tmp = esp_camera_fb_get();
    if (tmp) {
      esp_camera_fb_return(tmp);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    TRICHTER_LOG_ERROR(TRICHTER_ERR_CAMERA, ESP_FAIL,
                       "Camera capture failed - no frame buffer");
    return NULL;
  }

  TRICHTER_LOGI(TAG, "Image captured: %dx%d, %zu bytes", fb->width, fb->height,
                fb->len);
  return fb;
#else
  TRICHTER_LOGW(TAG, "Camera module is disabled in configuration");
  return NULL;
#endif
}

void camera_clear_fb(void) {
  camera_fb_t *fb = NULL;
  while (esp_camera_available_frames()) {
    TRICHTER_LOGI(TAG, "Clearing available camera frames...");
    fb = esp_camera_fb_get();
  }

  if (fb != NULL) {
    esp_camera_fb_return(fb);
  }
}
