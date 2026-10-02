// 开机动画:绿磷光风 —— "PASSPORT OS" 打字机光标 + 绿点进度条。
// 与首页同配色(黑底/磷光绿),时长约 0.9s;自带节拍,调用方不得持 LVGL 锁。
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 显示开机动画并阻塞约 0.9s。内部自己拿/放 LVGL 锁。
void boot_splash_run(void);

// 首页加载完成后调用:删除开机屏对象(动画屏此后不再可见)。
void boot_splash_end(void);

#ifdef __cplusplus
}
#endif
