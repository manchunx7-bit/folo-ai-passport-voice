// main/apps/voice/mode.c —— 链路通道实现
//
// 2026-09-12:通道由 BLE 换成 Wi-Fi UDP(契约见 docs/voice-udp-channel.md)。
// 本文件从"BLE/USB 双通道抽象"退化为"单通道转发":
//   - 原 mode_channel_up(APP_CHAN_USB) 恒 false(mode.c 旧实现直接 return false,
//     仓库里从来没有 usb_link 实现)→ USB 分支整体删除,它从来不可用;
//   - 原 ble_audio_* 全部换成 udp_audio_*;
//   - 对外签名保持 mode.h 不变 —— app_state.c 的状态机与 host 测试零改动。
#include "mode.h"
#include "audio_streamer.h"
#include "udp_audio.h"

bool mode_link_up(void)
{
    return udp_audio_event_subscribed();
}

bool mode_channel_up(uint8_t chan)
{
    // 单通道:无线(APP_CHAN_BLE)即 UDP 通道。USB 已移除,恒 false。
    return chan == APP_CHAN_BLE && udp_audio_event_subscribed();
}

int mode_send_event_line(uint8_t chan, const char *line, size_t len)
{
    if (chan != APP_CHAN_BLE) return -1;
    return udp_audio_notify_event(line, len);
}

int mode_send_event_line_important(uint8_t chan, const char *line, size_t len,
                                   uint32_t timeout_ms)
{
    if (chan != APP_CHAN_BLE) return -1;
    return udp_audio_notify_event_blocking(line, len, timeout_ms);
}

const char *mode_audio_format(uint8_t chan)
{
    (void)chan;
    // UDP 下仍是每帧一个独立 IMA ADPCM block(4:1,8KB/s):块自带
    // predictor/index 头,丢一块只损失 100ms 且不失步 —— 与 UDP 丢包模型
    // 天然契合,比"高带宽就不用压缩"的直觉更优(见 docs/voice-udp-channel.md §6)。
    return "ima_adpcm";
}

void mode_select_audio_sender(uint8_t chan)
{
    (void)chan;
    audio_streamer_set_sender(udp_audio_notify_audio);
    audio_streamer_set_compressed(true);
}

bool mode_wired(void)
{
    return false;   // USB 通道已移除
}
