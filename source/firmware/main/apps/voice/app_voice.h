#pragma once

#include "apps/app_base.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const passport_app_t g_voice_app;

void voice_app_init(void);
void voice_app_start(void);
void voice_app_stop(void);
void voice_app_on_key(uint8_t btn, app_btn_event_t event, uint16_t mv);

#ifdef __cplusplus
}
#endif
