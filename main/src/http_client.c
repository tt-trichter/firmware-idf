#include "http_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <sys/param.h>

static const char *TAG = "http_client";

static http_client_config_t default_config = {
    .host = "4.231.40.213",
    .port = 80,
    .auth_header = "Basic dHJpY2h0ZXI6c3VwZXItc2FmZS1wYXNzd29yZA==",
    .timeout_ms = 10000};

static http_client_config_t current_config;
static bool initialized = false;

typedef struct
{
  char *buffer;
  int buffer_size;
  int data_len;
} http_response_data_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
  http_response_data_t *response_data = (http_response_data_t *)evt->user_data;

  switch (evt->event_id)
  {
  case HTTP_EVENT_ERROR:
    ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
    break;
  case HTTP_EVENT_HEADER_SENT:
    ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
    break;
  case HTTP_EVENT_ON_HEADER:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
    break;
  case HTTP_EVENT_ON_HEADERS_COMPLETE:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADERS_COMPLETE");
    break;
  case HTTP_EVENT_ON_DATA:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
    if (response_data && response_data->buffer && evt->data_len > 0)
    {
      int copy_len = MIN(evt->data_len, response_data->buffer_size - response_data->data_len - 1);
      if (copy_len > 0)
      {
        memcpy(response_data->buffer + response_data->data_len, evt->data, copy_len);
        response_data->data_len += copy_len;
        response_data->buffer[response_data->data_len] = '\0';
      }
    }
    break;
  case HTTP_EVENT_ON_FINISH:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
    break;
  case HTTP_EVENT_DISCONNECTED:
    ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
    break;
  case HTTP_EVENT_REDIRECT:
    ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
    break;
  }
  return ESP_OK;
}

esp_err_t http_client_init(void)
{
  if (initialized)
  {
    return ESP_OK;
  }

  // Copy default configuration
  memcpy(&current_config, &default_config, sizeof(http_client_config_t));
  initialized = true;

  ESP_LOGI(TAG, "HTTP client initialized with host: %s:%d", current_config.host, current_config.port);
  return ESP_OK;
}

esp_err_t http_client_set_config(const http_client_config_t *config)
{
  if (!config)
  {
    return ESP_ERR_INVALID_ARG;
  }

  memcpy(&current_config, config, sizeof(http_client_config_t));
  initialized = true;

  ESP_LOGI(TAG, "HTTP client configuration updated");
  return ESP_OK;
}

esp_err_t http_client_upload_image(const camera_fb_t *image_fb, image_upload_response_t *response)
{
  if (!initialized)
  {
    ESP_LOGE(TAG, "HTTP client not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!image_fb || !response)
  {
    ESP_LOGE(TAG, "Invalid parameters");
    return ESP_ERR_INVALID_ARG;
  }

  // Initialize response
  memset(response, 0, sizeof(image_upload_response_t));
  response->result = ESP_FAIL;

  // Prepare response buffer
  char response_buffer[256] = {0};
  http_response_data_t response_data = {
      .buffer = response_buffer,
      .buffer_size = sizeof(response_buffer),
      .data_len = 0};

  // Create URL
  char url[128];
  snprintf(url, sizeof(url), "http://%s:%d/api/v1/images",
           current_config.host, current_config.port);

  // Configure HTTP client
  esp_http_client_config_t client_config = {
      .url = url,
      .event_handler = http_event_handler,
      .user_data = &response_data,
      .timeout_ms = current_config.timeout_ms,
      .buffer_size = 1024,
      .buffer_size_tx = 1024,
      .transport_type = HTTP_TRANSPORT_OVER_TCP,
      .skip_cert_common_name_check = false,
      .is_async = false,
      .use_global_ca_store = false,
      .crt_bundle_attach = NULL};

  esp_http_client_handle_t client = esp_http_client_init(&client_config);
  if (!client)
  {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = ESP_OK;

  // Set headers
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Authorization", current_config.auth_header);
  esp_http_client_set_header(client, "Content-Type", "image/jpeg");
  esp_http_client_set_header(client, "Accept", "text/plain");

  // Set post data
  esp_http_client_set_post_field(client, (const char *)image_fb->buf, image_fb->len);

  ESP_LOGI(TAG, "Uploading image: %zu bytes to %s", image_fb->len, url);
  ESP_LOGI(TAG, "HTTP client config: host=%s, port=%d, transport=TCP", current_config.host, current_config.port);

  // Perform request
  err = esp_http_client_perform(client);
  if (err == ESP_OK)
  {
    response->http_status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Image upload HTTP Status = %d, content_length = %lld",
             response->http_status_code,
             esp_http_client_get_content_length(client));

    if (response->http_status_code == 200 || response->http_status_code == 201)
    {
      // Success - copy the image resource name
      if (response_data.data_len > 0)
      {
        response->image_resource_name = malloc(response_data.data_len + 1);
        if (response->image_resource_name)
        {
          strcpy(response->image_resource_name, response_buffer);
          response->result = ESP_OK;
          ESP_LOGI(TAG, "Image uploaded successfully, resource name: %s",
                   response->image_resource_name);
        }
        else
        {
          ESP_LOGE(TAG, "Failed to allocate memory for image resource name");
          err = ESP_ERR_NO_MEM;
        }
      }
      else
      {
        ESP_LOGE(TAG, "Empty response from image upload");
        err = ESP_ERR_INVALID_RESPONSE;
      }
    }
    else
    {
      ESP_LOGE(TAG, "Image upload failed with HTTP status %d", response->http_status_code);
      err = ESP_ERR_HTTP_BASE + response->http_status_code;
    }
  }
  else
  {
    ESP_LOGE(TAG, "Image upload request failed: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);

  if (err != ESP_OK)
  {
    response->result = err;
  }

  return err;
}

esp_err_t http_client_create_run(const session_data_t *session_data,
                                 const char *image_resource_name,
                                 run_create_response_t *response)
{
  if (!initialized)
  {
    ESP_LOGE(TAG, "HTTP client not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!session_data || !image_resource_name || !response)
  {
    ESP_LOGE(TAG, "Invalid parameters");
    return ESP_ERR_INVALID_ARG;
  }

  // Initialize response
  memset(response, 0, sizeof(run_create_response_t));
  response->result = ESP_FAIL;

  // Create JSON payload
  cJSON *json = cJSON_CreateObject();
  if (!json)
  {
    ESP_LOGE(TAG, "Failed to create JSON object");
    return ESP_ERR_NO_MEM;
  }

  cJSON_AddNumberToObject(json, "rate", session_data->rate);
  cJSON_AddNumberToObject(json, "duration", session_data->duration);
  cJSON_AddNumberToObject(json, "volume", session_data->volume);
  cJSON_AddStringToObject(json, "image", image_resource_name);

  char *json_string = cJSON_Print(json);
  cJSON_Delete(json);

  if (!json_string)
  {
    ESP_LOGE(TAG, "Failed to convert JSON to string");
    return ESP_ERR_NO_MEM;
  }

  // Prepare response buffer
  char response_buffer[512] = {0};
  http_response_data_t response_data = {
      .buffer = response_buffer,
      .buffer_size = sizeof(response_buffer),
      .data_len = 0};

  // Create URL
  char url[128];
  snprintf(url, sizeof(url), "http://%s:%d/api/v1/runs",
           current_config.host, current_config.port);

  // Configure HTTP client
  esp_http_client_config_t client_config = {
      .url = url,
      .event_handler = http_event_handler,
      .user_data = &response_data,
      .timeout_ms = current_config.timeout_ms,
      .buffer_size = 1024,
      .buffer_size_tx = 1024,
      .transport_type = HTTP_TRANSPORT_OVER_TCP,
      .skip_cert_common_name_check = false,
      .is_async = false,
      .use_global_ca_store = false,
      .crt_bundle_attach = NULL};

  esp_http_client_handle_t client = esp_http_client_init(&client_config);
  if (!client)
  {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    free(json_string);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = ESP_OK;

  // Set headers
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Authorization", current_config.auth_header);
  esp_http_client_set_header(client, "Content-Type", "application/json");

  // Set post data
  esp_http_client_set_post_field(client, json_string, strlen(json_string));

  ESP_LOGI(TAG, "Creating run with JSON: %s", json_string);
  ESP_LOGI(TAG, "Sending to: %s", url);

  // Perform request
  err = esp_http_client_perform(client);
  if (err == ESP_OK)
  {
    response->http_status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Create run HTTP Status = %d, content_length = %lld",
             response->http_status_code,
             esp_http_client_get_content_length(client));

    if (response->http_status_code >= 200 && response->http_status_code < 300)
    {
      // Success
      if (response_data.data_len > 0)
      {
        response->response_body = malloc(response_data.data_len + 1);
        if (response->response_body)
        {
          strcpy(response->response_body, response_buffer);
        }
      }
      response->result = ESP_OK;
      ESP_LOGI(TAG, "Run created successfully");
    }
    else
    {
      ESP_LOGE(TAG, "Create run failed with HTTP status %d", response->http_status_code);
      if (response_data.data_len > 0)
      {
        ESP_LOGE(TAG, "Error response: %s", response_buffer);
      }
      err = ESP_ERR_HTTP_BASE + response->http_status_code;
    }
  }
  else
  {
    ESP_LOGE(TAG, "Create run request failed: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);
  free(json_string);

  if (err != ESP_OK)
  {
    response->result = err;
  }

  return err;
}

esp_err_t http_client_submit_session(const session_data_t *session_data,
                                     image_upload_response_t *upload_response,
                                     run_create_response_t *run_response)
{
  if (!session_data || !session_data->image_fb)
  {
    ESP_LOGE(TAG, "Invalid session data or missing image");
    return ESP_ERR_INVALID_ARG;
  }

  // Local response structures if not provided
  image_upload_response_t local_upload_response;
  run_create_response_t local_run_response;

  image_upload_response_t *upload_resp = upload_response ? upload_response : &local_upload_response;
  run_create_response_t *run_resp = run_response ? run_response : &local_run_response;

  ESP_LOGI(TAG, "Starting session submission...");

  // Step 1: Upload image
  esp_err_t err = http_client_upload_image(session_data->image_fb, upload_resp);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to upload image: %s", esp_err_to_name(err));
    return err;
  }

  if (!upload_resp->image_resource_name)
  {
    ESP_LOGE(TAG, "Image upload succeeded but no resource name received");
    return ESP_ERR_INVALID_RESPONSE;
  }

  // Step 2: Create run
  err = http_client_create_run(session_data, upload_resp->image_resource_name, run_resp);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to create run: %s", esp_err_to_name(err));
    // Clean up image response if we're using local storage
    if (!upload_response)
    {
      http_client_free_image_response(upload_resp);
    }
    return err;
  }

  ESP_LOGI(TAG, "Session submitted successfully");

  // Clean up local responses if not provided by caller
  if (!upload_response)
  {
    http_client_free_image_response(upload_resp);
  }
  if (!run_response)
  {
    http_client_free_run_response(run_resp);
  }

  return ESP_OK;
}

void http_client_free_image_response(image_upload_response_t *response)
{
  if (response && response->image_resource_name)
  {
    free(response->image_resource_name);
    response->image_resource_name = NULL;
  }
}

void http_client_free_run_response(run_create_response_t *response)
{
  if (response && response->response_body)
  {
    free(response->response_body);
    response->response_body = NULL;
  }
}
