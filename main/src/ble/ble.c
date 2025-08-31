#include "ble/ble.h"
#include "gap.h"
#include "gatt_svc.h"
#include "trichter_error.h"
#include "trichter_service.h"

static const char *TAG = "ble";

void ble_store_config_init(void);

static void on_stack_reset(int reason);
static void on_stack_sync(void);

static void on_stack_reset(int reason) {
  TRICHTER_LOGI(TAG, "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void) { adv_init(); }

void nimble_host_config_init(void) {
  ble_hs_cfg.reset_cb = on_stack_reset;
  ble_hs_cfg.sync_cb = on_stack_sync;
  ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  ble_store_config_init();
}

void nimble_host_task(void *param) {
  TRICHTER_LOGI(TAG, "nimble host task has been started!");

  nimble_port_run();

  vTaskDelete(NULL);
}
