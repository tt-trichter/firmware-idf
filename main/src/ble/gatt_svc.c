#include "ble/gatt_svc.h"
#include "ble/ble.h"
#include "ble/trichter_service.h"
#include "trichter_config.h"
#include "trichter_error.h"

static const char *TAG = "ble_gatt";

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
  char buf[BLE_UUID_STR_LEN];

  switch (ctxt->op) {

  case BLE_GATT_REGISTER_OP_SVC:
    TRICHTER_LOGI(TAG, "registered service %s with handle=%d",
                  ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                  ctxt->svc.handle);
    break;

  case BLE_GATT_REGISTER_OP_CHR:
    TRICHTER_LOGI(TAG,
                  "registering characteristic %s with "
                  "def_handle=%d val_handle=%d",
                  ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                  ctxt->chr.def_handle, ctxt->chr.val_handle);
    break;

  case BLE_GATT_REGISTER_OP_DSC:
    TRICHTER_LOGI(TAG, "registering descriptor %s with handle=%d",
                  ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                  ctxt->dsc.handle);
    break;

  default:
    assert(0);
    break;
  }
}

void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
  if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d",
             event->subscribe.conn_handle, event->subscribe.attr_handle);
  } else {
    ESP_LOGI(TAG, "subscribe by nimble stack; attr_handle=%d",
             event->subscribe.attr_handle);
  }

  trichter_ble_handle_subscribe(event);
}

int gatt_svc_init(void) {
  int rc;

  ble_svc_gatt_init();

  rc = trichter_ble_service_init();
  if (rc != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize Trichter BLE service");
    return rc;
  }

  ESP_LOGI(TAG, "GATT services initialized");
  return 0;
}
