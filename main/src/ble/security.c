#include "ble/security.h"

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "trichter_config.h"
#include <stdint.h>

static const char *TAG = "ble_sec";

void trichter_ble_show_passkey(uint32_t passkey) {
  TRICHTER_LOGW(TAG, "PAIRING PASSKEY: %06lu", (unsigned long)passkey);

  // TODO(display): show passkey on display
}

void trichter_ble_on_pairing_complete(int status) {
  if (status == 0) {
    ESP_LOGI(TAG, "Pairing complete: SUCCESS");
  } else {
    ESP_LOGW(TAG, "Pairing complete: FAILED (status=%d)", status);
  }
}

void trichter_ble_security_init(void) {
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 0;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 0;
  ble_hs_cfg.sm_our_key_dist = 0;
  ble_hs_cfg.sm_their_key_dist = 0;

  ESP_LOGI(TAG, "Security configured: bonding=%d mitm=%d sc=%d io_cap=%d",
           ble_hs_cfg.sm_bonding, ble_hs_cfg.sm_mitm, ble_hs_cfg.sm_sc,
           ble_hs_cfg.sm_io_cap);
}

// void trichter_ble_security_init(void) {
//   // IO capability: DisplayOnly (trichter show passkey, phone enters it)
//   ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
//
//   // Bonding: store keys in NVS via ble_store
//   ble_hs_cfg.sm_bonding = 1;
//
//   // MITM: require passkey
//   ble_hs_cfg.sm_mitm = 1;
//
//   // Secure Connections (LESC): modern crypto
//   ble_hs_cfg.sm_sc = 1;
//
//   // If you want to allow legacy pairing fallback (old phones), keep this 0.
//   // ble_hs_cfg.sm_sc = 0;
//
//   // Key distribution (what trichter gives / what trichter want)
//   // EncKey = LTK, IdKey = IRK/identity, SignKey = CSRK
//   ble_hs_cfg.sm_our_key_dist =
//       BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
//   ble_hs_cfg.sm_their_key_dist =
//       BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
//
//   ESP_LOGI(TAG, "Security configured: bonding=%d mitm=%d sc=%d io_cap=%d",
//            ble_hs_cfg.sm_bonding, ble_hs_cfg.sm_mitm, ble_hs_cfg.sm_sc,
//            ble_hs_cfg.sm_io_cap);
// }

