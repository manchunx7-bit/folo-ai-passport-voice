#pragma once

#include "apps/app_base.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化应用注册表
void app_registry_init(void);

// 获取应用数量（不含 Launcher）
size_t app_registry_get_count(void);

// 获取指定索引的应用描述
const passport_app_t *app_registry_get_by_index(size_t index);

// 获取指定 ID 的应用描述
const passport_app_t *app_registry_get_by_id(passport_app_id_t id);

// 获取当前处于前台激活的应用 ID
passport_app_id_t app_registry_get_active(void);

// 切换前台应用（若传入 APP_ID_LAUNCHER 则返回主菜单）
void app_registry_switch_to(passport_app_id_t id);

// 全局按键统一分发（含 1.5s 长按 OK 拦截与熄屏唤醒保护）
void app_registry_dispatch_key(uint8_t btn, app_btn_event_t event, uint16_t mv);

// 由系统心跳调用；统一处理跨 App 的息屏，不额外创建常驻任务。
void app_registry_tick(void);
void app_registry_set_screen_timeout(uint16_t seconds);
uint16_t app_registry_get_screen_timeout(void);

// 活动(app 活动)续期:非按键驱动的应用(如声控游戏)在持续运行时调用,
// 抑制息屏计时。只刷新计时,不会点亮已熄灭的屏幕。
void app_registry_keep_awake(void);

#ifdef __cplusplus
}
#endif
