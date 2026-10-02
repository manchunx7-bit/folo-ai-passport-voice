#pragma once

#include "apps/app_base.h"

#ifdef __cplusplus
extern "C" {
#endif

// NVS 初始化后、Launcher 创建前调用，恢复整机设置并立即应用。
void settings_bootstrap(void);

// Shared by Settings and app volume keys; applies and persists only volume.
void settings_set_volume(uint8_t percent);

extern const passport_app_t g_settings_app;

#ifdef __cplusplus
}
#endif
