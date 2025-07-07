#include "server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "camera.h"

static const char *TAG = "server";

static const httpd_uri_t jpg_image_uri = {
    .uri       = "/jpg",
    .method    = HTTP_GET,
    .handler   = camera_jpg_image_http_handler,
    .user_ctx  = NULL
};

httpd_handle_t server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting server on port %d", config.server_port);
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &jpg_image_uri);
        return server;
    }
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return NULL;
}

esp_err_t server_stop(httpd_handle_t server)
{
    ESP_LOGI(TAG, "Stopping server");
    return httpd_stop(server);
}
