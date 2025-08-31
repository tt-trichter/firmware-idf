#include "ble/trichter_service.h"
#include "ble/ble.h"
#include "esp_log.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "trichter_ble_svc";

static trichter_ble_state_t ble_state = {0};
static trichter_session_callback_t session_callback = NULL;

static uint16_t status_char_handle;
static uint16_t result_char_handle;
static uint16_t control_char_handle;
static uint16_t image_char_handle;

static uint8_t *image_buffer = NULL;
static size_t image_size = 0;
static size_t image_offset = 0;

static const ble_uuid128_t trichter_svc_uuid = TRICHTER_SERVICE_UUID;
static const ble_uuid128_t status_char_uuid = TRICHTER_STATUS_CHAR_UUID;
static const ble_uuid128_t result_char_uuid = TRICHTER_RESULT_CHAR_UUID;
static const ble_uuid128_t control_char_uuid = TRICHTER_CONTROL_CHAR_UUID;
static const ble_uuid128_t image_char_uuid = TRICHTER_IMAGE_CHAR_UUID;

static int trichter_status_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
static int trichter_result_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
static int trichter_control_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt,
                                   void *arg);
static int trichter_image_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def trichter_gatt_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &trichter_svc_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {
                 .uuid = &status_char_uuid.u,
                 .access_cb = trichter_status_access,
                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                 .val_handle = &status_char_handle,
             },
             {
                 .uuid = &result_char_uuid.u,
                 .access_cb = trichter_result_access,
                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_INDICATE,
                 .val_handle = &result_char_handle,
             },
             {
                 .uuid = &control_char_uuid.u,
                 .access_cb = trichter_control_access,
                 .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
                 .val_handle = &control_char_handle,
             },
             {
                 .uuid = &image_char_uuid.u,
                 .access_cb = trichter_image_access,
                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                 .val_handle = &image_char_handle,
             },
             {
                 0,
             }}},
    {
        0,
    },
};

static int trichter_status_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg) {
  int rc = 0;

  switch (ctxt->op) {
  case BLE_GATT_ACCESS_OP_READ_CHR:
    ESP_LOGD(TAG, "Status characteristic read; conn_handle=%d", conn_handle);
    rc = os_mbuf_append(ctxt->om, &ble_state.current_status,
                        sizeof(ble_state.current_status));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

  default:
    ESP_LOGE(TAG, "Unexpected operation on status characteristic: %d",
             ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
  }
}

static int trichter_result_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg) {
  int rc = 0;

  switch (ctxt->op) {
  case BLE_GATT_ACCESS_OP_READ_CHR:
    ESP_LOGD(TAG, "Result characteristic read; conn_handle=%d", conn_handle);

    if (ble_state.current_result != NULL) {
      trichter_ble_result_t ble_result = {
          .duration_ms =
              (uint32_t)(ble_state.current_result->duration_us / 1000),
          .rate_lpm = ble_state.current_result->rate_lpm,
          .volume_l = ble_state.current_result->volume_l,
          .has_image = ble_state.current_result->image_fb != NULL ? 1 : 0,
          .image_size = ble_state.current_result->image_fb
                            ? ble_state.current_result->image_fb->len
                            : 0};

      rc = os_mbuf_append(ctxt->om, &ble_result, sizeof(ble_result));
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else {
      return BLE_ATT_ERR_INVALID_HANDLE;
    }

  default:
    ESP_LOGE(TAG, "Unexpected operation on result characteristic: %d",
             ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
  }
}

static int trichter_control_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt,
                                   void *arg) {
  switch (ctxt->op) {
  case BLE_GATT_ACCESS_OP_WRITE_CHR:
    ESP_LOGD(TAG, "Control characteristic write; conn_handle=%d", conn_handle);

    if (ctxt->om->om_len >= 1) {
      uint8_t cmd = ctxt->om->om_data[0];
      ESP_LOGI(TAG, "Received control command: %d", cmd);

      if (cmd == TRICHTER_CMD_ACKNOWLEDGE) {
        ble_state.waiting_for_ack = false;
        ESP_LOGI(TAG, "Session acknowledged by peer");
      }

      if (session_callback) {
        session_callback((trichter_control_cmd_t)cmd);
      }

      return 0;
    } else {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

  default:
    ESP_LOGE(TAG, "Unexpected operation on control characteristic: %d",
             ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
  }
}

static int trichter_image_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
  int rc = 0;

  switch (ctxt->op) {
  case BLE_GATT_ACCESS_OP_READ_CHR:
    ESP_LOGD(TAG, "Image characteristic read; conn_handle=%d", conn_handle);

    if (image_buffer && image_size > 0) {
      size_t chunk_size = 20;
      size_t remaining = image_size - image_offset;
      size_t to_send = (remaining > chunk_size) ? chunk_size : remaining;

      rc = os_mbuf_append(ctxt->om, image_buffer + image_offset, to_send);
      if (rc == 0) {
        image_offset += to_send;
        ESP_LOGD(TAG, "Sent image chunk: %zu/%zu bytes", image_offset,
                 image_size);
      }

      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else {
      return BLE_ATT_ERR_INVALID_HANDLE;
    }

  default:
    ESP_LOGE(TAG, "Unexpected operation on image characteristic: %d", ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
  }
}

static bool svc_added = false;
esp_err_t trichter_ble_service_init(void) {
  if(svc_added) {
    TRICHTER_LOGE(TAG, "TRIED TO ADD SERVICE TWICE");
    return ESP_FAIL;
  }
  int rc;

  memset(&ble_state, 0, sizeof(ble_state));
  ble_state.current_status = TRICHTER_STATUS_IDLE;

  rc = ble_gatts_count_cfg(trichter_gatt_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to count GATT services: %d", rc);
    return ESP_FAIL;
  }

  rc = ble_gatts_add_svcs(trichter_gatt_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Trichter BLE service initialized");
  svc_added = true;
  return ESP_OK;
}

void trichter_ble_set_status(trichter_session_status_t status) {
  ble_state.current_status = status;

  if (ble_state.connected && ble_state.status_subscribed) {
    int rc = ble_gatts_notify(ble_state.conn_handle, status_char_handle);
    if (rc == 0) {
      ESP_LOGD(TAG, "Status notification sent: %d", status);
    } else {
      ESP_LOGW(TAG, "Failed to send status notification: %d", rc);
    }
  }
}

void trichter_ble_send_result(const SessionResult *result) {
  if (!result) {
    ESP_LOGE(TAG, "Invalid result pointer");
    return;
  }

  ble_state.current_result = (SessionResult *)result;
  ble_state.waiting_for_ack = true;

  if (result->image_fb && result->image_fb->len > 0) {
    image_buffer = result->image_fb->buf;
    image_size = result->image_fb->len;
    image_offset = 0;
    ESP_LOGI(TAG, "Image data prepared: %zu bytes", image_size);
  } else {
    image_buffer = NULL;
    image_size = 0;
    image_offset = 0;
    TRICHTER_LOGI(TAG, "No image data");
  }

  TRICHTER_LOGI(TAG, "Connected: %d - Subscribed: %d", ble_state.conn_handle,
                ble_state.result_subscribed);
  if (ble_state.connected && ble_state.result_subscribed) {
    int rc = ble_gatts_indicate(ble_state.conn_handle, result_char_handle);
    if (rc == 0) {
      ESP_LOGI(TAG,
               "Result indication sent - Duration: %.2fs, Rate: %.2f L/min, "
               "Volume: %.2f L",
               result->duration_us / 1e6f, result->rate_lpm, result->volume_l);
    } else {
      ESP_LOGW(TAG, "Failed to send result indication: %d", rc);
    }
  }
}

bool trichter_ble_is_connected(void) { return ble_state.connected; }

bool trichter_ble_is_waiting_for_ack(void) { return ble_state.waiting_for_ack; }

void trichter_ble_on_connect(uint16_t conn_handle) {
  ble_state.connected = true;
  ble_state.conn_handle = conn_handle;
  ble_state.waiting_for_ack = false;

  ESP_LOGI(TAG, "BLE connected: handle=%d", conn_handle);
}

void trichter_ble_on_disconnect(void) {
  ble_state.connected = false;
  ble_state.status_subscribed = false;
  ble_state.result_subscribed = false;
  ble_state.image_subscribed = false;
  ble_state.waiting_for_ack = false;
  ble_state.conn_handle = 0;

  image_buffer = NULL;
  image_size = 0;
  image_offset = 0;

  ESP_LOGI(TAG, "BLE disconnected");
}

void trichter_ble_cleanup_result(void) {
  ble_state.current_result = NULL;
  ble_state.waiting_for_ack = false;

  image_buffer = NULL;
  image_size = 0;
  image_offset = 0;
}

void trichter_ble_set_session_callback(trichter_session_callback_t callback) {
  session_callback = callback;
}

void trichter_ble_handle_subscribe(struct ble_gap_event *event) {
  const uint16_t handle = event->subscribe.attr_handle;
  const bool notify = event->subscribe.cur_notify;
  const bool indicate = event->subscribe.cur_indicate;

  ESP_LOGI(TAG, "[SUB] conn=%u handle=%u notify=%u indicate=%u",
           event->subscribe.conn_handle, handle, notify, indicate);

  if (handle == status_char_handle) {
    ble_state.status_subscribed = notify;
    ESP_LOGI(TAG, "Status notifications %s", notify ? "enabled" : "disabled");

  } else if (handle == result_char_handle) {
    ble_state.result_subscribed = (notify || indicate);
    ESP_LOGI(TAG, "Result indications %s",
             ble_state.result_subscribed ? "enabled" : "disabled");

  } else if (handle == image_char_handle) {
    ble_state.image_subscribed = notify;
    ESP_LOGI(TAG, "Image notifications %s", notify ? "enabled" : "disabled");

  } else {
    ESP_LOGW(TAG, "Subscribe on unknown value handle: %u", handle);
    ESP_LOGI(TAG, "Known handles: status=%u result=%u control=%u image=%u",
             status_char_handle, result_char_handle, control_char_handle,
             image_char_handle);
  }
}

const char *trichter_status_to_string(trichter_session_status_t status) {
  switch (status) {
  case TRICHTER_STATUS_IDLE:
    return "IDLE";
  case TRICHTER_STATUS_WAITING:
    return "WAITING";
  case TRICHTER_STATUS_RUNNING:
    return "RUNNING";
  case TRICHTER_STATUS_COMPLETE:
    return "COMPLETE";
  case TRICHTER_STATUS_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

const char *trichter_cmd_to_string(trichter_control_cmd_t cmd) {
  switch (cmd) {
  case TRICHTER_CMD_ACKNOWLEDGE:
    return "ACKNOWLEDGE";
  case TRICHTER_CMD_RESET:
    return "RESET";
  default:
    return "UNKNOWN";
  }
}

void trichter_ble_print_state(void) {
  ESP_LOGI(TAG, "=== Trichter BLE State ===");
  ESP_LOGI(TAG, "Connected: %s", ble_state.connected ? "YES" : "NO");
  ESP_LOGI(TAG, "Connection Handle: %d", ble_state.conn_handle);
  ESP_LOGI(TAG, "Current Status: %s (%d)",
           trichter_status_to_string(ble_state.current_status),
           ble_state.current_status);
  ESP_LOGI(TAG, "Status Subscribed: %s",
           ble_state.status_subscribed ? "YES" : "NO");
  ESP_LOGI(TAG, "Result Subscribed: %s",
           ble_state.result_subscribed ? "YES" : "NO");
  ESP_LOGI(TAG, "Image Subscribed: %s",
           ble_state.image_subscribed ? "YES" : "NO");
  ESP_LOGI(TAG, "Waiting for ACK: %s",
           ble_state.waiting_for_ack ? "YES" : "NO");
  ESP_LOGI(TAG, "Current Result: %s",
           ble_state.current_result ? "AVAILABLE" : "NULL");
  if (image_buffer && image_size > 0) {
    ESP_LOGI(TAG, "Image Buffer: %zu bytes, offset: %zu", image_size,
             image_offset);
  } else {
    ESP_LOGI(TAG, "Image Buffer: None");
  }
  ESP_LOGI(TAG, "========================");
}
