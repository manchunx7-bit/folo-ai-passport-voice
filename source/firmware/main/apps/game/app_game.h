// 《你在狗叫什么》声控魔性 App。
// 内存策略(上线硬约束):
//   1. 零大缓冲 —— 不用 canvas/framebuffer,鸟和管道全部是常驻小对象池(约40个
//      lv_obj,~5KB),游戏过程零堆分配;
//   2. 不另起渲染任务 —— 游戏循环是 33ms 的 lv_timer,跑在 LVGL 任务里;
//      唯一的额外任务是采音(3KB 栈,读满即算,与 UI 只隔一个原子变量);
//   3. stop() 全量回收:先删定时器再拆屏,采音任务join后返回,堆回到进入前水平。
#pragma once

#include "apps/app_base.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const passport_app_t g_game_app;

#ifdef __cplusplus
}
#endif
