#pragma once

#include "esp_err.h"
#include "sensor/sensor.h"
#include "trichter_error.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  APP_STATE_INIT,
  APP_STATE_READY,
  APP_STATE_WAITING_SESSION,
  APP_STATE_SESSION_RUNNING,
  APP_STATE_SESSION_COMPLETE,
  APP_STATE_ERROR
} app_state_t;

typedef struct {
  app_state_t state;
  bool ble_enabled;
  bool ble_connected;
  bool camera_enabled;
  bool display_enabled;
  bool sensor_enabled;
  SessionResult current_session;
} app_context_t;

typedef struct {
  app_state_t from;
  app_state_t to;
  bool valid;
} state_transition_t;

static inline const char *app_state_to_string(app_state_t state);

static const state_transition_t valid_transitions[] = {
    {APP_STATE_INIT, APP_STATE_READY, true},
    {APP_STATE_READY, APP_STATE_WAITING_SESSION, true},
    {APP_STATE_WAITING_SESSION, APP_STATE_SESSION_RUNNING, true},
    {APP_STATE_SESSION_RUNNING, APP_STATE_SESSION_COMPLETE, true},
    {APP_STATE_SESSION_RUNNING, APP_STATE_ERROR, true},
    {APP_STATE_SESSION_COMPLETE, APP_STATE_WAITING_SESSION, true},
    {APP_STATE_ERROR, APP_STATE_WAITING_SESSION, true},
    {APP_STATE_ERROR, APP_STATE_READY, true},
    {APP_STATE_WAITING_SESSION, APP_STATE_ERROR, true},
    {APP_STATE_SESSION_COMPLETE, APP_STATE_ERROR, true},
};

static inline bool is_valid_state_transition(app_state_t from, app_state_t to) {
  if (from == to)
    return true; // Same state is always valid

  const size_t num_transitions =
      sizeof(valid_transitions) / sizeof(valid_transitions[0]);

  for (size_t i = 0; i < num_transitions; i++) {
    if (valid_transitions[i].from == from && valid_transitions[i].to == to &&
        valid_transitions[i].valid) {
      return true;
    }
  }

  return false;
}

static inline esp_err_t app_state_set(app_context_t *ctx,
                                      app_state_t new_state) {
  TRICHTER_CHECK(ctx != NULL, TRICHTER_ERR_STATE, "Invalid app context");

  if (!is_valid_state_transition(ctx->state, new_state)) {
    TRICHTER_LOG_ERROR(TRICHTER_ERR_STATE, ESP_ERR_INVALID_STATE,
                       "Invalid state transition attempted");
    return ESP_ERR_INVALID_STATE;
  }

  TRICHTER_LOGI("TRICHTER_STATE", "Transitioning from %s to %s.", app_state_to_string(ctx->state), app_state_to_string(new_state));

  ctx->state = new_state;
  return ESP_OK;
}

static inline bool app_can_start_session(const app_context_t *ctx) {
  if (!ctx || !ctx->sensor_enabled) {
    return false;
  }

  return (ctx->state == APP_STATE_WAITING_SESSION);
}

static inline bool app_is_in_error(const app_context_t *ctx) {
  return ctx && (ctx->state == APP_STATE_ERROR);
}

static inline bool app_session_active(const app_context_t *ctx) {
  return ctx && (ctx->state == APP_STATE_SESSION_RUNNING);
}

static inline const char *app_state_to_string(app_state_t state) {
  switch (state) {
  case APP_STATE_INIT:
    return "INIT";
  case APP_STATE_READY:
    return "READY";
  case APP_STATE_WAITING_SESSION:
    return "WAITING_SESSION";
  case APP_STATE_SESSION_RUNNING:
    return "SESSION_RUNNING";
  case APP_STATE_SESSION_COMPLETE:
    return "SESSION_COMPLETE";
  case APP_STATE_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

