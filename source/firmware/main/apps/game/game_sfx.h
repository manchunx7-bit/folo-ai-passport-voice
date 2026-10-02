// 《你在狗叫什么》音效(game_sfx):16kHz 方波,复用 bsp_audio 的 TX 通道。
//
// 为什么单独一个模块而不是复用 apps/voice 的 app_sound:
//   app_sound 依赖 voice 的 app_events/app_types,并且会再跑一次 bsp_audio_init
//   + set_format —— 与采音任务并发 open/close codec 有竞态。本模块只做
//   bsp_audio_write(codec 已由采音任务打开,全双工,TX/RX 是两条独立 I2S
//   handle),codec 没就绪时静默跳过,绝不反向驱动 codec 状态。
//
// 成本:一个 2KB 栈的静态优先级任务,只在游戏前台存在;播放请求走任务通知,
// 不占队列 RAM,也不会阻塞 LVGL 主循环(否则 60ms 的方波 = 掉 2 帧)。
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GS_NONE = 0,
    GS_START,   /* 开局:短促上扬 */
    GS_SCORE,   /* 过一根管道:清脆一声 */
    GS_DEAD,    /* 死亡:下行两音 */
    GS_BEST,    /* 破纪录:三音上行小号 */
    GS_COUNT,
} game_sfx_t;

// 建播放任务(幂等)。失败只影响音效,不影响玩法。
void game_sfx_start(void);

// 删播放任务(幂等)。退出游戏时调用,回收栈。
void game_sfx_stop(void);

// 异步请求播放一个音;未启动/队列忙时静默丢弃(音效永远不能拖慢主循环)。
void game_sfx_play(game_sfx_t id);

#ifdef __cplusplus
}
#endif
