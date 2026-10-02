// 声控采音:16kHz/16bit/单声道读麦克风,输出 0..4095 的平滑响度。
// 独立小任务(3KB 栈),与 LVGL 之间只过一个原子变量 —— 游戏循环零锁零等待。
//
// 2026-09-13 优化:
//   * 新增 game_mic_ready() —— 区分"环境安静"和"麦克风还没热机",上层可据此
//     决定是否显示提示,而不是把热机期的 0 误判成"玩家没叫";
//   * game_mic_start() 现在能可靠重入:退出后立刻重进 App 时,旧任务若还在
//     收尾(s_task 非空)会先等它退出再建新任务,否则会静默跳过建任务 ——
//     那会导致这一局完全没有声控(原来踩过)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 启动采音任务(幂等;若上一轮任务尚未退出会先等它退出)。
// 内部 bsp_audio_init/set_format(16k) 与语音应用同配置,
// App 互斥运行所以不存在抢占 I2S 的问题。
void game_mic_start(void);

// 请求退出并等待任务结束(每次 read 最长 16ms,通常 <50ms 返回)。
void game_mic_stop(void);

// 平滑响度(均值整流×EMA)。静音房间约数百,正常说话数千。
// 未就绪(热机期)恒为 0。
uint16_t game_mic_level(void);

// 麦克风是否已过上电瞬态、输出可信。热机期约 160ms。
bool game_mic_ready(void);

#ifdef __cplusplus
}
#endif
