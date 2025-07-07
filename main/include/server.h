#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

httpd_handle_t server_start(void);

esp_err_t server_stop(httpd_handle_t server);

