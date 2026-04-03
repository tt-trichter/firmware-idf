#include "ble/gap.h"
#include "ble/ble.h"
#include "ble/gatt_svc.h"
#include "ble/security.h"
#include "ble/trichter_service.h"
#include "host/ble_gap.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "trichter_error.h"
#include <inttypes.h>
#include <stdint.h>

static const char *TAG = "ble_gap";

inline static void format_addr(char *addr_str, uint8_t addr[]);
static void print_conn_desc(struct ble_gap_conn_desc *desc);
static void start_advertising(void);
static int gap_event_handler(struct ble_gap_event *event, void *arg);

static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};
static uint8_t esp_uri[] = {BLE_GAP_URI_PREFIX_HTTPS,
                            '/',
                            '/',
                            't',
                            'r',
                            'i',
                            'c',
                            'h',
                            't',
                            'e',
                            'r',
                            '.',
                            'i',
                            'o'};

inline static void format_addr(char *addr_str, uint8_t addr[]) {
  sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2],
          addr[3], addr[4], addr[5]);
}

static void print_conn_desc(struct ble_gap_conn_desc *desc) {
  char addr_str[18] = {0};

  TRICHTER_LOGI(TAG, "connection handle: %d", desc->conn_handle);

  format_addr(addr_str, desc->our_id_addr.val);
  TRICHTER_LOGI(TAG, "device id address: type=%d, value=%s",
                desc->our_id_addr.type, addr_str);

  format_addr(addr_str, desc->peer_id_addr.val);
  TRICHTER_LOGI(TAG, "peer id address: type=%d, value=%s",
                desc->peer_id_addr.type, addr_str);

  TRICHTER_LOGI(TAG,
                "conn_itvl=%d, conn_latency=%d, supervision_timeout=%d, "
                "encrypted=%d, authenticated=%d, bonded=%d\n",
                desc->conn_itvl, desc->conn_latency, desc->supervision_timeout,
                desc->sec_state.encrypted, desc->sec_state.authenticated,
                desc->sec_state.bonded);
}

static void start_advertising(void) {
  int rc;
  const char *name = ble_svc_gap_device_name();

  struct ble_hs_adv_fields adv_fields = {0};
  struct ble_hs_adv_fields rsp_fields = {0};
  struct ble_gap_adv_params adv_params = {0};

  // General discoverable, LE only
  adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  // Put the 128-bit service UUID here so scanners can filter without relying on
  // scan response
  static const ble_uuid128_t trichter_uuid = TRICHTER_SERVICE_UUID;
  adv_fields.uuids128 = &trichter_uuid;
  adv_fields.num_uuids128 = 1;
  adv_fields.uuids128_is_complete = 1;

  adv_fields.appearance = BLE_GAP_APPEARANCE_GENERIC_SENSOR;
  adv_fields.appearance_is_present = 1;

  rc = ble_gap_adv_set_fields(&adv_fields);
  if (rc != 0) {
    TRICHTER_LOGE(TAG, "failed to set advertising data rc=%d", rc);
    // If you can, log the error string too:
    // ESP_LOGE(TAG, "adv_set_fields rc=%d (%s)", rc, ble_hs_err_to_str(rc));
    return;
  }

  rsp_fields.name = (uint8_t *)name;
  rsp_fields.name_len = strlen(name);
  rsp_fields.name_is_complete = 1;

  rsp_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  rsp_fields.tx_pwr_lvl_is_present = 1;

  rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
  if (rc != 0) {
    TRICHTER_LOG_ERROR(TRICHTER_ERR_BLE, rc,
                       "failed to set scan response data");
    return;
  }
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(300);
  adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(350);

  rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                         gap_event_handler, NULL);
  if (rc != 0) {
    TRICHTER_LOG_ERROR(TRICHTER_ERR_BLE, rc, "failed to start advertising");
    return;
  }

  TRICHTER_LOGI(TAG, "advertising started!");
}

static int gap_event_handler(struct ble_gap_event *event, void *arg) {
  int rc = 0;
  struct ble_gap_conn_desc desc;

  switch (event->type) {

  case BLE_GAP_EVENT_CONNECT:
    TRICHTER_LOGI(TAG, "connection %s; status=%d",
                  event->connect.status == 0 ? "established" : "failed",
                  event->connect.status);

    if (event->connect.status == 0) {
      rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
      if (rc != 0) {
        TRICHTER_LOG_ERROR(TRICHTER_ERR_BLE, rc,
                           "failed to find connection by handle");
        return rc;
      }

      print_conn_desc(&desc);
      trichter_ble_on_connect(event->connect.conn_handle);

      struct ble_gap_upd_params params = {.itvl_min = desc.conn_itvl,
                                          .itvl_max = desc.conn_itvl,
                                          .latency = 3,
                                          .supervision_timeout =
                                              desc.supervision_timeout};
      rc = ble_gap_update_params(event->connect.conn_handle, &params);
      if (rc != 0) {
        ESP_LOGE(TAG, "failed to update connection parameters, error code: %d",
                 rc);
      }

      rc = ble_gap_security_initiate(event->connect.conn_handle);
      if (rc != 0) {
        TRICHTER_LOGW(TAG, "ble_gap_security_initiate failed: %d", rc);
      }

    } else {
      start_advertising();
    }
    return rc;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "disconnected from peer; reason=%d",
             event->disconnect.reason);

    trichter_ble_on_disconnect();
    start_advertising();
    return rc;

  case BLE_GAP_EVENT_CONN_UPDATE:
    ESP_LOGI(TAG, "connection updated; status=%d", event->conn_update.status);

    rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
    if (rc != 0) {
      ESP_LOGE(TAG, "failed to find connection by handle, error code: %d", rc);
      return rc;
    }
    print_conn_desc(&desc);
    return rc;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    ESP_LOGI(TAG, "advertise complete; reason=%d", event->adv_complete.reason);
    start_advertising();
    return rc;

  case BLE_GAP_EVENT_NOTIFY_TX:
    if ((event->notify_tx.status != 0) &&
        (event->notify_tx.status != BLE_HS_EDONE)) {
      ESP_LOGI(TAG,
               "notify event; conn_handle=%d attr_handle=%d "
               "status=%d is_indication=%d",
               event->notify_tx.conn_handle, event->notify_tx.attr_handle,
               event->notify_tx.status, event->notify_tx.indication);
    }
    return rc;

  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(TAG,
             "subscribe event; conn_handle=%d attr_handle=%d "
             "reason=%d prevn=%d curn=%d previ=%d curi=%d",
             event->subscribe.conn_handle, event->subscribe.attr_handle,
             event->subscribe.reason, event->subscribe.prev_notify,
             event->subscribe.cur_notify, event->subscribe.prev_indicate,
             event->subscribe.cur_indicate);

    gatt_svr_subscribe_cb(event);
    return rc;

  case BLE_GAP_EVENT_MTU:
    ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d",
             event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
    return rc;

  case BLE_GAP_EVENT_ENC_CHANGE:
    ESP_LOGI(TAG, "Encryption change; status=%d", event->enc_change.status);

    // if status==0 encryption is on, check conn dev for bonded/auth flags
    if (event->enc_change.status == 0) {
      rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
      if (rc == 0) {
        ESP_LOGI(TAG, "Encrypted=%d Authenticated=%d Bonded?%d",
                 desc.sec_state.encrypted, desc.sec_state.authenticated,
                 desc.sec_state.bonded);
      }
    }
    return 0;

  case BLE_GAP_EVENT_PASSKEY_ACTION: {
    const uint16_t ch = event->passkey.conn_handle;
    const uint8_t action = event->passkey.params.action;

    TRICHTER_LOGI(TAG, "PASSKEY_ACTION: conn=%u action=%u", ch, action);

    if (action == BLE_SM_IOACT_DISP) {
      const uint32_t passkey = trichter_generate_passkey();

      // Placeholder now: log (later call your display driver)
      trichter_ble_show_passkey(passkey);

      struct ble_sm_io io = {0};
      io.action = BLE_SM_IOACT_DISP;
      io.passkey = passkey;

      int rc = ble_sm_inject_io(ch, &io);
      if (rc != 0) {
        TRICHTER_LOGE(TAG, "ble_sm_inject_io(DISP) failed: %d", rc);
      }
      return 0;
    }

    TRICHTER_LOGW(TAG, "Unhandled passkey action=%u", action);
    return 0;
  }

  case BLE_GAP_EVENT_REPEAT_PAIRING:
    TRICHTER_LOGW(TAG,
                  "Repeat pairing requested - deleting old bond and retrying");

    rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
    if (rc == 0) {
      int del_rc = ble_store_util_delete_peer(&desc.peer_id_addr);
      TRICHTER_LOGW(TAG, "ble_store_util_delete_peer rc=%d", del_rc);
    }
    return BLE_GAP_REPEAT_PAIRING_RETRY;
  }

  return rc;
}

void adv_init(void) {
  int rc = 0;
  char addr_str[18] = {0};

  rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    ESP_LOGE(TAG, "device does not have any available bt address!");
    return;
  }

  rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to infer address type, error code: %d", rc);
    return;
  }

  rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to copy device address, error code: %d", rc);
    return;
  }
  format_addr(addr_str, addr_val);
  ESP_LOGI(TAG, "device address: %s", addr_str);

  start_advertising();
}

int gap_init(void) {
  int rc = 0;

  ble_svc_gap_init();

  rc = ble_svc_gap_device_name_set(DEVICE_NAME);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to set device name to %s, error code: %d",
             DEVICE_NAME, rc);
    return rc;
  }
  return rc;
}
