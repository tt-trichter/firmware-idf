#pragma once

#include "esp_err.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "sensor/sensor.h"
#include <stdbool.h>
#include <stdint.h>

// Trichter BLE Service UUID: 12345678-1234-5678-9ABC-DEF012345678
#define TRICHTER_SERVICE_UUID                                                  \
  BLE_UUID128_INIT(0x78, 0x56, 0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A, 0x78, 0x56, \
                   0x34, 0x12, 0x78, 0x56, 0x34, 0x12)

// Session Status Characteristic UUID: 12345678-1234-5678-9ABC-DEF012345679
#define TRICHTER_STATUS_CHAR_UUID                                              \
  BLE_UUID128_INIT(0x79, 0x56, 0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A, 0x78, 0x56, \
                   0x34, 0x12, 0x78, 0x56, 0x34, 0x12)

// Session Result Characteristic UUID: 12345678-1234-5678-9ABC-DEF01234567A
#define TRICHTER_RESULT_CHAR_UUID                                              \
  BLE_UUID128_INIT(0x7A, 0x56, 0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A, 0x78, 0x56, \
                   0x34, 0x12, 0x78, 0x56, 0x34, 0x12)

// Control Characteristic UUID: 12345678-1234-5678-9ABC-DEF01234567B
#define TRICHTER_CONTROL_CHAR_UUID                                             \
  BLE_UUID128_INIT(0x7B, 0x56, 0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A, 0x78, 0x56, \
                   0x34, 0x12, 0x78, 0x56, 0x34, 0x12)

// Image Data Characteristic UUID: 12345678-1234-5678-9ABC-DEF01234567C
#define TRICHTER_IMAGE_CHAR_UUID                                               \
  BLE_UUID128_INIT(0x7C, 0x56, 0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A, 0x78, 0x56, \
                   0x34, 0x12, 0x78, 0x56, 0x34, 0x12)

#define TRICHTER_IMAGE_CHUNK_FLAG_START 0x01
#define TRICHTER_IMAGE_CHUNK_FLAG_END   0x02
#define TRICHTER_IMAGE_BURST_CHUNKS 8

typedef enum {
  TRICHTER_STATUS_WAITING = 0x01,
  TRICHTER_STATUS_RUNNING = 0x02,
  TRICHTER_STATUS_COMPLETE = 0x03,
  TRICHTER_STATUS_ERROR = 0x04
} trichter_session_status_t;

typedef enum {
  TRICHTER_CMD_ACKNOWLEDGE = 0x02,
  TRICHTER_CMD_RESET = 0x03,
  TRICHTER_CMD_FAKE_RUN = 0x04,
  TRICHTER_CMD_IMAGE_START = 0x05,
  TRICHTER_CMD_IMAGE_CANCEL = 0x06,
  TRICHTER_CMD_IMAGE_ACK = 0x07,
  TRICHTER_CMD_IMAGE_RECEIVED = 0x08,
} trichter_control_cmd_t;

typedef struct {
    bool active;
    uint32_t transfer_id;
    uint8_t *buffer;
    size_t size;
    size_t offset;
    uint16_t mtu;
} trichter_image_transfer_t;

typedef struct __attribute__((packed)) {
  uint32_t duration_ms;
  float rate_lpm;
  float volume_l;
  uint8_t has_image;
  uint32_t image_size;
  uint16_t image_width;
  uint16_t image_height;
  uint8_t image_format;
  uint32_t image_transfer_id;
} trichter_ble_result_t;

typedef struct __attribute__((packed)) {
    uint32_t transfer_id;
    uint32_t offset;
    uint16_t payload_len;
    uint8_t flags;
} trichter_image_chunk_header_t;

typedef struct {
  bool connected;
  bool status_subscribed;
  bool result_subscribed;
  bool image_subscribed;
  bool waiting_for_ack;
  uint16_t conn_handle;
  trichter_session_status_t current_status;
  SessionResult *current_result;
} trichter_ble_state_t;

esp_err_t trichter_ble_service_init(void);
void trichter_ble_notify_status(void);
void trichter_ble_set_status(trichter_session_status_t status);
void trichter_ble_send_result(const SessionResult *result);
bool trichter_ble_is_connected(void);
bool trichter_ble_is_waiting_for_ack(void);
void trichter_ble_on_connect(uint16_t conn_handle);
void trichter_ble_on_disconnect(void);
void trichter_ble_cleanup_result(void);

typedef void (*trichter_session_callback_t)(trichter_control_cmd_t cmd);
void trichter_ble_set_session_callback(trichter_session_callback_t callback);

const char *trichter_status_to_string(trichter_session_status_t status);
const char *trichter_cmd_to_string(trichter_control_cmd_t cmd);
void trichter_ble_print_state(void);

void trichter_ble_handle_subscribe(struct ble_gap_event *event);
