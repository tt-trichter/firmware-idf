#pragma once

#include "esp_err.h"
#include "esp_camera.h"

typedef struct
{
  const char *host;
  int port;
  const char *auth_header;
  int timeout_ms;
} http_client_config_t;

typedef struct
{
  float rate;
  float duration;
  float volume;
  camera_fb_t *image_fb;
} session_data_t;

typedef struct
{
  esp_err_t result;
  char *image_resource_name;
  int http_status_code;
} image_upload_response_t;

typedef struct
{
  esp_err_t result;
  int http_status_code;
  char *response_body;
} run_create_response_t;

esp_err_t http_client_init(void);

esp_err_t http_client_set_config(const http_client_config_t *config);

esp_err_t http_client_upload_image(const camera_fb_t *image_fb, image_upload_response_t *response);

esp_err_t http_client_create_run(const session_data_t *session_data,
                                 const char *image_resource_name,
                                 run_create_response_t *response);
esp_err_t http_client_submit_session(const session_data_t *session_data,
                                     image_upload_response_t *upload_response,
                                     run_create_response_t *run_response);

void http_client_free_image_response(image_upload_response_t *response);

void http_client_free_run_response(run_create_response_t *response);
