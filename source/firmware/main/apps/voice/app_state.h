// Pure reducer for the hardware microphone / PC hotkey controller.
#pragma once
#include "app_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    app_stage_t state;
    bool link_up;
    bool pc_ready;
    bool net_busy;
    bool screen_on;
    bool panel_on;
    bool stream_started;
    uint64_t pc_status_ms;
    uint64_t state_since_ms;
    uint64_t toast_until_ms;
    char toast[APP_TOAST_MAX];
} app_state_t;
void app_state_init(app_state_t *s);
void app_state_reduce(app_state_t *s, const app_event_t *ev,
                      uint64_t now_ms, app_action_t *out, uint8_t *out_n);
void app_state_snapshot(const app_state_t *s, uint64_t now_ms, app_ui_snapshot_t *snap);
#ifdef __cplusplus
}
#endif
