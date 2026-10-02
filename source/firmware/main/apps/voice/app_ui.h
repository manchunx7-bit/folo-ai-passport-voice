// WeType remote: a single screen with link/readiness/PTT telemetry.
#pragma once

#include "app_types.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 建立单页麦克风遥控界面。须在 bsp_display_init + bsp_lvgl_init 成功后调用,
// 且调用方须持 LVGL 锁(bsp_lvgl_lock)。
esp_err_t app_ui_init(void);
void app_ui_deinit(void);

void app_ui_show(void);
void app_ui_hide(void);

// 按快照刷新 UI。同样须持锁调用(唯一写者:app_task)。
// 整机息屏/唤醒由 app_registry 管理。
void app_ui_render(const app_ui_snapshot_t *snap);

#ifdef __cplusplus
}
#endif
