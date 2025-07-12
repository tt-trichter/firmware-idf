#include "hal/lv_hal_disp.h"
#include "sensor.h"

lv_disp_t *display_init(void);

void display_write_await_session(lv_disp_t *disp);

void display_write_result(lv_disp_t *disp, const SessionResult *res);
