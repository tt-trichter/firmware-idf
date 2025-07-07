#include "camera.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "camera";

static camera_config_t camera_config = {
    .pin_pwdn       = PWDN_GPIO_NUM,
    .pin_reset      = RESET_GPIO_NUM,
    .pin_xclk       = XCLK_GPIO_NUM,
    .pin_sccb_sda   = SIOD_GPIO_NUM,
    .pin_sccb_scl   = SIOC_GPIO_NUM,
    .pin_d7         = Y9_GPIO_NUM,
    .pin_d6         = Y8_GPIO_NUM,
    .pin_d5         = Y7_GPIO_NUM,
    .pin_d4         = Y6_GPIO_NUM,
    .pin_d3         = Y5_GPIO_NUM,
    .pin_d2         = Y4_GPIO_NUM,
    .pin_d1         = Y3_GPIO_NUM,
    .pin_d0         = Y2_GPIO_NUM,
    .pin_vsync      = VSYNC_GPIO_NUM,
    .pin_href       = HREF_GPIO_NUM,
    .pin_pclk       = PCLK_GPIO_NUM,
    .xclk_freq_hz   = 20000000,
    .fb_location    = CAMERA_FB_IN_PSRAM,
    .pixel_format   = PIXFORMAT_JPEG,
    .frame_size     = FRAMESIZE_UXGA,
    .jpeg_quality   = 12,
    .fb_count       = 2,
    .grab_mode      = CAMERA_GRAB_WHEN_EMPTY
};

esp_err_t camera_init_module(void)
{
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed (%d)", err);
    }
    return err;
}

typedef struct {
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

static size_t jpg_encode_stream(void *arg, size_t index, const void* data, size_t len)
{
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

esp_err_t camera_jpg_image_http_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");

    esp_err_t res;
    if (fb->format == PIXFORMAT_JPEG) {
        res = httpd_resp_send(req, (const char *)fb->buf, fb->len) == ESP_OK
            ? ESP_OK : ESP_FAIL;
    } else {
        jpg_chunking_t jchunk = { .req = req, .len = 0 };
        res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk)
            ? ESP_OK : ESP_FAIL;
        httpd_resp_send_chunk(req, NULL, 0);
    }

    esp_camera_fb_return(fb);
    return res;
}
