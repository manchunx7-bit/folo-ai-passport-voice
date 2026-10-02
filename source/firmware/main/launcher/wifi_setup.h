#pragma once

#include "apps/app_base.h"

#ifdef __cplusplus
extern "C" {
#endif

void wifi_setup_init(void);
void wifi_setup_start(void);
void wifi_setup_stop(void);
void wifi_setup_on_key(uint8_t btn, app_btn_event_t event, uint16_t mv);
void wifi_setup_require_initial(void);
bool wifi_setup_can_leave(void);
void wifi_setup_tick(void);

extern const passport_app_t g_wifi_app;

#ifdef __cplusplus
}
#endif
