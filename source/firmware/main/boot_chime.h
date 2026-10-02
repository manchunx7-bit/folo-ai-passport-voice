// 首页个人资料之外的开机彩蛋:复古 8-bit 开机音(E5-G5-B5-E6 方波琶音)。
// 纯程序合成,无音频素材;专用短命任务里播放,播完立即 bsp_audio_deinit
// 把 codec/DMA 内存还给系统 —— 开机只借不占。
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 创建一次性任务播放开机音(异步,不阻塞启动流程)。重复调用无害。
void boot_chime_start(void);

#ifdef __cplusplus
}
#endif
