#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(radio_font);
LV_FONT_DECLARE(radio_font_title);
// 4360 字中文字库(SourceHanSansSC 16px 子集)。首页的姓名/签名/天气/提示、
// 收音机的中文回退都依赖它 —— radio_font 只是小子集,覆盖不了。原属 codex
// 应用目录,2026-09-12 随 AI设备 模块移除迁到 fonts/,避免被一起删掉。
// 也用于 AI语音 的**动态文本**(电脑下发的转写/审批正文,字符不可预知)。
LV_FONT_DECLARE(buddy_font_16);
// AI语音 标题/提示字体(20px/4bpp,反锯齿)。只含该界面静态文案用到的字 ——
// 改 main/apps/voice/app_ui.c 的中文文案后必须重新生成:
//   bash _diag/gen_voice_font.sh(见 main/fonts/voice_font_20.c 文件头)
LV_FONT_DECLARE(voice_font_20);
// 全机共享标题字体(20px/4bpp,SourceHanSansSC 子集)。覆盖 Launcher/设置/小智
// 的静态中文标题与提示 —— 改这些界面的中文文案后必须重新生成:
//   wsl -d Ubuntu-24.04 bash /mnt/f/WORK/AI硬件/_diag/gen_ui_font.sh
LV_FONT_DECLARE(ui_font_20);

#define APP_FONT_BODY  (&radio_font)
#define APP_FONT_TITLE (&radio_font_title)

#ifdef __cplusplus
}
#endif
