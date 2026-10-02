// 俄罗斯方块 App。
// 内存策略(与声控飞鸟同一套纪律):
//   1. 唯一大对象是棋盘画布 80x160 RGB565 = 25.6KB,start() 堆分配/stop() 释放
//      (进入前最大连续块 ~106KB,余量 4 倍);游戏过程零堆分配;
//   2. 游戏循环 = 50ms lv_timer,不另起任务;逻辑在 tetris_core(纯整数);
//   3. 结算面板用"移出屏幕"显隐 —— HIDDEN 标志路径在 LVGL 9.3 有死循环雷(见 app_game);
//   4. stop() 后堆回到进入前水平。
#pragma once

#include "apps/app_base.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const passport_app_t g_tetris_app;

#ifdef __cplusplus
}
#endif
