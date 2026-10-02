#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "apps/app_base.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 Launcher UI 资源
void launcher_ui_init(void);

// 显示 Launcher 主界面（隐藏子应用界面）
void launcher_ui_show(void);

// 隐藏 Launcher 主界面（进入子应用前调用）
void launcher_ui_hide(void);

// Launcher 界面按键处理（UP/DOWN 翻卡片，OK 进入）
void launcher_ui_on_key(uint8_t btn, app_btn_event_t event);

// 更新状态栏信息（电池、时间）
void launcher_ui_update_status(void);

#ifdef __cplusplus
}
#endif
