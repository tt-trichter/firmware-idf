#include "ble/trichter_service.h"
#include "ble/ble.h"
#include "esp_log.h"
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "services/gatt/ble_svc_gatt.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "trichter_ble_svc";

static trichter_ble_state_t ble_state = {0};
static trichter_session_callback_t session_callback = NULL;

static uint16_t status_char_handle;
static uint16_t result_char_handle;
static uint16_t control_char_handle;
static uint16_t image_char_handle;

static const ble_uuid128_t trichter_svc_uuid = TRICHTER_SERVICE_UUID;
static const ble_uuid128_t status_char_uuid = TRICHTER_STATUS_CHAR_UUID;
static const ble_uuid128_t result_char_uuid = TRICHTER_RESULT_CHAR_UUID;
static const ble_uuid128_t control_char_uuid = TRICHTER_CONTROL_CHAR_UUID;
static const ble_uuid128_t image_char_uuid = TRICHTER_IMAGE_CHAR_UUID;

static trichter_image_transfer_t image_tx = {0};

static int trichter_status_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
static int trichter_result_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
static int trichter_control_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt,
                                   void *arg);
static int trichter_image_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);

static void trichter_ble_reset_image_transfer(void);
static void trichter_ble_prepare_image_transfer(const SessionResult *result);
static void trichter_ble_start_image_transfer(void);
static void trichter_ble_cancel_image_transfer(void);
static void trichter_ble_send_next_image_chunk(void);
static void trichter_ble_send_image_burst(void);

static const struct ble_gatt_svc_def trichter_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
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
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .val_handle = &control_char_handle,
                },
                {
                    .uuid = &image_char_uuid.u,
                    .access_cb = trichter_image_access,
                    .flags = BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &image_char_handle,
                },
                {0},
            },
    },
    {0},
};

static int trichter_status_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg) {
  int rc = 0;

  switch (ctxt->op) {
  case BLE_GATT_ACCESS_OP_READ_CHR:
    TRICHTER_LOGD(TAG, "Status characteristic read; conn_handle=%u",
                  conn_handle);
    rc = os_mbuf_append(ctxt->om, &ble_state.current_status,
                        sizeof(ble_state.current_status));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

  default:
    TRICHTER_LOGE(TAG, "Unexpected operation on status characteristic: %d",
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
    TRICHTER_LOGD(TAG, "Result characteristic read; conn_handle=%u",
                  conn_handle);

    if (ble_state.current_result != NULL) {
      trichter_ble_result_t ble_result = {
          .duration_ms =
              (uint32_t)(ble_state.current_result->duration_us / 1000ULL),
          .rate_lpm = ble_state.current_result->rate_lpm,
          .volume_l = ble_state.current_result->volume_l,
          .has_image = ble_state.current_result->image_fb != NULL ? 1 : 0,
          .image_size = ble_state.current_result->image_fb
                            ? ble_state.current_result->image_fb->len
                            : 0,
          .image_width =
              ble_state.current_result->image_fb
                  ? (uint16_t)ble_state.current_result->image_fb->width
                  : 0,
          .image_height =
              ble_state.current_result->image_fb
                  ? (uint16_t)ble_state.current_result->image_fb->height
                  : 0,
          .image_format =
              ble_state.current_result->image_fb
                  ? (uint8_t)ble_state.current_result->image_fb->format
                  : 0,
          .image_transfer_id = image_tx.transfer_id,
      };

      rc = os_mbuf_append(ctxt->om, &ble_result, sizeof(ble_result));
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else {
      TRICHTER_LOGW(TAG, "Result read rejected: no result available");
      return BLE_ATT_ERR_INVALID_HANDLE;
    }

  default:
    TRICHTER_LOGE(TAG, "Unexpected operation on result characteristic: %d",
                  ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
  }
}

static int trichter_control_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt,
                                   void *arg) {
  switch (ctxt->op) {
  case BLE_GATT_ACCESS_OP_WRITE_CHR:
    TRICHTER_LOGD(TAG, "Control characteristic write; conn_handle=%u",
                  conn_handle);

    if (ctxt->om->om_len >= 1) {
      uint8_t cmd = ctxt->om->om_data[0];
      TRICHTER_LOGI(TAG, "Received control command: %u", cmd);

      if (cmd == TRICHTER_CMD_ACKNOWLEDGE) {
        ble_state.waiting_for_ack = false;
        TRICHTER_LOGI(TAG, "Session acknowledged by peer");
      }

      switch ((trichter_control_cmd_t)cmd) {
      case TRICHTER_CMD_IMAGE_START:
        trichter_ble_start_image_transfer();
        break;

      case TRICHTER_CMD_IMAGE_ACK:
        trichter_ble_send_image_burst();
        break;

      case TRICHTER_CMD_IMAGE_CANCEL:
        trichter_ble_cancel_image_transfer();
        break;

      default:
        break;
      }

      if (session_callback) {
        session_callback((trichter_control_cmd_t)cmd);
      }

      return 0;
    } else {
      TRICHTER_LOGW(TAG, "Control write rejected: missing command byte");
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

  default:
    TRICHTER_LOGE(TAG, "Unexpected operation on control characteristic: %d",
                  ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
  }
}

static int trichter_image_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
  TRICHTER_LOGW(TAG, "Image characteristic is notify-only; op=%d", ctxt->op);
  return BLE_ATT_ERR_UNLIKELY;
}

static void trichter_ble_reset_image_transfer(void) {
  if (image_tx.buffer) {
    free(image_tx.buffer);
    image_tx.buffer = NULL;
  }

  image_tx.size = 0;
  image_tx.offset = 0;
  image_tx.active = false;
}

static void trichter_ble_prepare_image_transfer(const SessionResult *result) {
  trichter_ble_reset_image_transfer();

  if (!result || !result->image_fb || !result->image_fb->buf ||
      result->image_fb->len == 0) {
    TRICHTER_LOGI(TAG, "No image data available for transfer");
    return;
  }

  TRICHTER_LOGI(TAG, "3. Image timestamp: %d",
                result->image_fb->timestamp.tv_sec);
  uint8_t *copy = malloc(result->image_fb->len);
  if (!copy) {
    TRICHTER_LOGE(TAG, "Failed to allocate image transfer buffer: %zu bytes",
                  result->image_fb->len);
    return;
  }

  memcpy(copy, result->image_fb->buf, result->image_fb->len);

  image_tx.transfer_id++;
  image_tx.buffer = copy;
  image_tx.size = result->image_fb->len;
  image_tx.offset = 0;
  image_tx.active = false;

  TRICHTER_LOGI(
      TAG,
      "Prepared image transfer: id=%lu size=%zu width=%u height=%u format=%d",
      (unsigned long)image_tx.transfer_id, image_tx.size,
      result->image_fb->width, result->image_fb->height,
      result->image_fb->format);
}

static void trichter_ble_start_image_transfer(void) {
  if (!ble_state.connected) {
    TRICHTER_LOGW(TAG, "Cannot start image transfer: not connected");
    return;
  }

  if (!ble_state.image_subscribed) {
    TRICHTER_LOGW(
        TAG, "Cannot start image transfer: image notifications not subscribed");
    return;
  }

  if (!image_tx.buffer || image_tx.size == 0) {
    TRICHTER_LOGW(TAG, "Cannot start image transfer: no image prepared");
    return;
  }

  image_tx.offset = 0;
  image_tx.active = true;

  TRICHTER_LOGI(TAG, "Starting image transfer: id=%lu size=%zu conn=%u",
                (unsigned long)image_tx.transfer_id, image_tx.size,
                ble_state.conn_handle);

  ESP_LOGI(TAG, "Starting image transfer: id=%lu size=%zu conn=%u",
           (unsigned long)image_tx.transfer_id, image_tx.size,
           ble_state.conn_handle);

  trichter_ble_send_image_burst();
}

static void trichter_ble_cancel_image_transfer(void) {
  if (image_tx.active) {
    TRICHTER_LOGI(
        TAG, "Cancelling active image transfer: id=%lu offset=%zu/%zu",
        (unsigned long)image_tx.transfer_id, image_tx.offset, image_tx.size);
  } else {
    TRICHTER_LOGI(TAG,
                  "Image transfer cancel requested, but no transfer active");
  }

  image_tx.active = false;
  image_tx.offset = 0;
}

static void trichter_ble_send_next_image_chunk(void) {
  if (!ble_state.connected) {
    TRICHTER_LOGW(TAG, "Cannot send image chunk: not connected");
    image_tx.active = false;
    return;
  }

  if (!ble_state.image_subscribed) {
    TRICHTER_LOGW(
        TAG, "Cannot send image chunk: image notifications not subscribed");
    image_tx.active = false;
    return;
  }

  if (!image_tx.active) {
    TRICHTER_LOGD(TAG, "Image ACK/next received but transfer is not active");
    return;
  }

  if (!image_tx.buffer || image_tx.size == 0) {
    TRICHTER_LOGW(TAG, "Cannot send image chunk: no image buffer");
    image_tx.active = false;
    return;
  }

  if (image_tx.offset >= image_tx.size) {
    TRICHTER_LOGI(TAG, "Image transfer already complete");
    image_tx.active = false;
    return;
  }

  uint16_t mtu = ble_att_mtu(ble_state.conn_handle);
  TRICHTER_LOGI(TAG, "Transferring image with MTU of %d", mtu);
  size_t max_packet = mtu > 3 ? (size_t)(mtu - 3) : 0; // ATT notify payload
  size_t header_size = sizeof(trichter_image_chunk_header_t);

  if (max_packet <= header_size) {
    TRICHTER_LOGE(TAG, "MTU too small for image transfer: mtu=%u header=%zu",
                  mtu, header_size);
    image_tx.active = false;
    return;
  }

  size_t max_payload = max_packet - header_size;
  size_t remaining = image_tx.size - image_tx.offset;
  size_t chunk_len = remaining < max_payload ? remaining : max_payload;

  trichter_image_chunk_header_t hdr = {
      .transfer_id = image_tx.transfer_id,
      .offset = (uint32_t)image_tx.offset,
      .payload_len = (uint16_t)chunk_len,
      .flags = 0,
  };

  if (image_tx.offset == 0) {
    hdr.flags |= TRICHTER_IMAGE_CHUNK_FLAG_START;
  }
  if (image_tx.offset + chunk_len >= image_tx.size) {
    hdr.flags |= TRICHTER_IMAGE_CHUNK_FLAG_END;
  }

  size_t packet_len = header_size + chunk_len;
  uint8_t *packet = malloc(packet_len);
  if (!packet) {
    TRICHTER_LOGE(TAG, "Failed to allocate packet buffer for image chunk");
    image_tx.active = false;
    return;
  }

  memcpy(packet, &hdr, header_size);
  memcpy(packet + header_size, image_tx.buffer + image_tx.offset, chunk_len);

  struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, packet_len);
  free(packet);

  if (!om) {
    TRICHTER_LOGE(TAG, "Failed to allocate mbuf for image chunk");
    image_tx.active = false;
    return;
  }

  int rc =
      ble_gatts_notify_custom(ble_state.conn_handle, image_char_handle, om);
  if (rc != 0) {
    TRICHTER_LOGE(
        TAG,
        "Failed to send image chunk: rc=%d id=%lu offset=%zu len=%zu mtu=%u",
        rc, (unsigned long)image_tx.transfer_id, image_tx.offset, chunk_len,
        mtu);
    image_tx.active = false;
    return;
  }

  TRICHTER_LOGI(TAG,
                "Image chunk sent: id=%lu offset=%zu len=%zu "
                "remaining_after=%zu flags=0x%02x mtu=%u",
                (unsigned long)image_tx.transfer_id, image_tx.offset, chunk_len,
                image_tx.size - (image_tx.offset + chunk_len), hdr.flags, mtu);

  image_tx.offset += chunk_len;

  if (image_tx.offset >= image_tx.size) {
    TRICHTER_LOGI(TAG, "Image transfer complete: id=%lu total=%zu",
                  (unsigned long)image_tx.transfer_id, image_tx.size);
    image_tx.active = false;
  }
}

static void trichter_ble_send_image_burst(void) {
  for (int i = 0; i < TRICHTER_IMAGE_BURST_CHUNKS; ++i) {
    if (!image_tx.active) {
      return;
    }

    trichter_ble_send_next_image_chunk();

    if (!image_tx.active) {
      return;
    }

    vTaskDelay(pdMS_TO_TICKS(0));
  }
}

static bool svc_added = false;

esp_err_t trichter_ble_service_init(void) {
  if (svc_added) {
    TRICHTER_LOGE(TAG, "Tried to add service twice");
    return ESP_FAIL;
  }

  int rc;

  memset(&ble_state, 0, sizeof(ble_state));
  memset(&image_tx, 0, sizeof(image_tx));
  ble_state.current_status = TRICHTER_STATUS_IDLE;

  rc = ble_gatts_count_cfg(trichter_gatt_svcs);
  if (rc != 0) {
    TRICHTER_LOGE(TAG, "Failed to count GATT services: %d", rc);
    return ESP_FAIL;
  }

  rc = ble_gatts_add_svcs(trichter_gatt_svcs);
  if (rc != 0) {
    TRICHTER_LOGE(TAG, "Failed to add GATT services: %d", rc);
    return ESP_FAIL;
  }

  TRICHTER_LOGI(TAG, "Trichter BLE service initialized");
  svc_added = true;
  return ESP_OK;
}

void trichter_ble_set_status(trichter_session_status_t status) {
  if (status == ble_state.current_status) {
    return; // no change — skip redundant notification
  }
  ble_state.current_status = status;

  if (ble_state.connected && ble_state.status_subscribed) {
    TRICHTER_LOGI(TAG, "Status notification: %d", status);
    int rc = ble_gatts_notify(ble_state.conn_handle, status_char_handle);
    if (rc != 0) {
      TRICHTER_LOGW(TAG, "Failed to send status notification: %d", rc);
    }
  }
}

void trichter_ble_send_result(const SessionResult *result) {
  if (!result) {
    TRICHTER_LOGE(TAG, "Invalid result pointer");
    return;
  }

  ble_state.current_result = (SessionResult *)result;
  ble_state.waiting_for_ack = true;

  trichter_ble_prepare_image_transfer(result);

  TRICHTER_LOGI(TAG, "Connected=%d result_subscribed=%d image_subscribed=%d",
                ble_state.connected, ble_state.result_subscribed,
                ble_state.image_subscribed);

  if (ble_state.connected && ble_state.result_subscribed) {
    int rc = ble_gatts_indicate(ble_state.conn_handle, result_char_handle);
    if (rc == 0) {
      TRICHTER_LOGI(TAG,
                    "Result indication sent - Duration: %.2fs, Rate: %.2f "
                    "L/min, Volume: %.2f L, HasImage: %d, ImageSize: %zu",
                    result->duration_us / 1e6f, result->rate_lpm,
                    result->volume_l, result->image_fb != NULL ? 1 : 0,
                    result->image_fb ? result->image_fb->len : 0);
    } else {
      TRICHTER_LOGW(TAG, "Failed to send result indication: %d", rc);
    }
  } else {
    TRICHTER_LOGW(TAG, "Result not indicated: connected=%d subscribed=%d",
                  ble_state.connected, ble_state.result_subscribed);
  }
}

bool trichter_ble_is_connected(void) { return ble_state.connected; }

bool trichter_ble_is_waiting_for_ack(void) { return ble_state.waiting_for_ack; }

void trichter_ble_on_connect(uint16_t conn_handle) {
  ble_state.connected = true;
  ble_state.conn_handle = conn_handle;
  ble_state.waiting_for_ack = false;

  TRICHTER_LOGI(TAG, "BLE connected: handle=%u", conn_handle);

  trichter_ble_print_state();
}

void trichter_ble_on_disconnect(void) {
  ble_state.connected = false;
  ble_state.status_subscribed = false;
  ble_state.result_subscribed = false;
  ble_state.image_subscribed = false;
  ble_state.waiting_for_ack = false;
  ble_state.conn_handle = 0;
  // Reset cached status so the first notification after reconnect is always sent
  ble_state.current_status = (trichter_session_status_t)-1;

  trichter_ble_reset_image_transfer();

  TRICHTER_LOGI(TAG, "BLE disconnected");

  trichter_ble_print_state();
}

void trichter_ble_cleanup_result(void) {
  ble_state.current_result = NULL;
  ble_state.waiting_for_ack = false;
  trichter_ble_reset_image_transfer();
}

void trichter_ble_set_session_callback(trichter_session_callback_t callback) {
  session_callback = callback;
}

void trichter_ble_handle_subscribe(struct ble_gap_event *event) {
  const uint16_t handle = event->subscribe.attr_handle;
  const bool notify = event->subscribe.cur_notify;
  const bool indicate = event->subscribe.cur_indicate;

  TRICHTER_LOGI(TAG, "[SUB] conn=%u handle=%u notify=%u indicate=%u",
                event->subscribe.conn_handle, handle, notify, indicate);

  if (handle == status_char_handle) {
    ble_state.status_subscribed = notify;
    TRICHTER_LOGI(TAG, "Status notifications %s",
                  notify ? "enabled" : "disabled");

  } else if (handle == result_char_handle) {
    ble_state.result_subscribed = (notify || indicate);
    TRICHTER_LOGI(TAG, "Result indications %s",
                  ble_state.result_subscribed ? "enabled" : "disabled");

  } else if (handle == image_char_handle) {
    ble_state.image_subscribed = notify;
    TRICHTER_LOGI(TAG, "Image notifications %s",
                  notify ? "enabled" : "disabled");

  } else {
    TRICHTER_LOGW(TAG, "Subscribe on unknown value handle: %u", handle);
    TRICHTER_LOGI(TAG, "Known handles: status=%u result=%u control=%u image=%u",
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
  case TRICHTER_CMD_FAKE_RUN:
    return "FAKE_RUN";
  case TRICHTER_CMD_IMAGE_START:
    return "IMAGE_START";
  case TRICHTER_CMD_IMAGE_ACK:
    return "IMAGE_ACK";
  case TRICHTER_CMD_IMAGE_CANCEL:
    return "IMAGE_CANCEL";
  default:
    return "UNKNOWN";
  }
}

void trichter_ble_print_state(void) {
  TRICHTER_LOGI(TAG, "=== Trichter BLE State ===");
  TRICHTER_LOGI(TAG, "Connected: %s", ble_state.connected ? "YES" : "NO");
  TRICHTER_LOGI(TAG, "Connection Handle: %u", ble_state.conn_handle);
  TRICHTER_LOGI(TAG, "Current Status: %s (%d)",
                trichter_status_to_string(ble_state.current_status),
                ble_state.current_status);
  TRICHTER_LOGI(TAG, "Status Subscribed: %s",
                ble_state.status_subscribed ? "YES" : "NO");
  TRICHTER_LOGI(TAG, "Result Subscribed: %s",
                ble_state.result_subscribed ? "YES" : "NO");
  TRICHTER_LOGI(TAG, "Image Subscribed: %s",
                ble_state.image_subscribed ? "YES" : "NO");
  TRICHTER_LOGI(TAG, "Waiting for ACK: %s",
                ble_state.waiting_for_ack ? "YES" : "NO");
  TRICHTER_LOGI(TAG, "Current Result: %s",
                ble_state.current_result ? "AVAILABLE" : "NULL");

  TRICHTER_LOGI(TAG, "Image Transfer Active: %s",
                image_tx.active ? "YES" : "NO");
  TRICHTER_LOGI(TAG, "Image Transfer ID: %lu",
                (unsigned long)image_tx.transfer_id);
  TRICHTER_LOGI(TAG, "Image Transfer Size: %zu", image_tx.size);
  TRICHTER_LOGI(TAG, "Image Transfer Offset: %zu", image_tx.offset);
  TRICHTER_LOGI(TAG, "Image Buffer: %s", image_tx.buffer ? "SET" : "NULL");
  TRICHTER_LOGI(TAG, "========================");
}
