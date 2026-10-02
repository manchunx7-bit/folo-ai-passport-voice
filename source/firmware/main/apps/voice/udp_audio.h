// main/apps/voice/udp_audio.h —— Wi-Fi UDP 音频/事件通道(取代 BLE)。
//
// 契约见 docs/voice-udp-channel.md —— 设备端与 companion 端共同遵守,改一侧
// 必须同步另一侧。要点:
//
//   端口 UDP 33333(双向同端口)
//   companion = 服务端:bind 33333,每 1000ms 向 255.255.255.255:33333 广播 beacon
//   设备      = 客户端:bind 33333(收 beacon),学到电脑 IP 后单播 <pc_ip>:33333
//
//   报文 = [1B type][payload]
//     0x01 AUDIO  设备→电脑  [seq][0x80] + 804B IMA ADPCM block  = 807B
//     0x02 EVENT  双向       UTF-8 JSON 行(含结尾 '\n')          ≤ 513B
//     0x03 BEACON 电脑→设备  {"v":1,"role":"pc","port":33333}     ~40B
//     0x04 PING   设备→电脑  空载荷,本端每 2s 一发,只用于判活     = 1B
//
// PING 的存在理由:设备空闲(没在录音、没有事件)时不发任何东西,companion 就
// 无法把"设备还在语音应用里"和"设备退出了语音应用"区分开。每 2s 一发即可让
// companion 的链路判活(6s 静默判断开)正常工作。
//
// AUDIO 保留 BLE 时代的 2B 分片帧头([块序号][片序号|0x80 末片]):UDP 下恒为
// "单片 + 末片" → [seq][0x80]。保留它是为了 companion 的 reassemble_adpcm()
// 一行都不用改;seq 每块 ++ mod 256,重复/迟到块由 companion 现有 last_seq 逻辑丢弃。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_UDP_PORT      33333
#define VOICE_UDP_T_AUDIO   0x01
#define VOICE_UDP_T_EVENT   0x02
#define VOICE_UDP_T_BEACON  0x03
#define VOICE_UDP_T_PING    0x04   // 设备→电脑 判活(companion 不派发,只刷链路时刻)
#define VOICE_UDP_PING_MS   2000u  // 判活周期(companion 侧 6s 静默判断开)

// 对端静默多久判链路断开(与 beacon 周期 1000ms 成 3:1 冗余)
#define VOICE_UDP_PEER_TIMEOUT_MS 3000u

// ---- 生命周期 ----
// 建 UDP socket、bind 33333 并起 RX 任务。幂等:已初始化直接返回 0。
// 返回 0 = 成功;非 0 = socket/bind/任务创建失败。
int udp_audio_init(void);
// 停 RX 任务并关 socket(先置停止位等任务退出,再关 socket,避免 recvfrom 竞态)。
// 幂等:未初始化调用无副作用。
int udp_audio_deinit(void);

// ---- 上行(与旧 ble_audio_* 语义一一对应) ----
// 音频块:frame = audio_streamer 产出的 804B IMA ADPCM block(非 3200B PCM)。
// 组装 [0x01][seq][0x80][block] 后单播。对端未知/发送失败返回非 0(调用方计丢帧)。
int udp_audio_notify_audio(const uint8_t *frame, size_t len);
// 事件行(≤512B,含结尾 '\n'):组装 [0x02][line] 后单播。非阻塞。
int udp_audio_notify_event(const char *line, size_t len);
// UDP 无发送队列与背压,本函数与 udp_audio_notify_event 等价 —— 保留签名只为
// 让 app_voice.cc 的会话边界帧(voice.start/end)调用点不用改。
int udp_audio_notify_event_blocking(const char *line, size_t len, uint32_t timeout_ms);

// ---- 状态查询 ----
bool udp_audio_connected(void);       // 对端已知且未被判超时
bool udp_audio_event_subscribed(void); // link_up 依据(与 connected 同义,UDP 无订阅概念)
uint32_t udp_audio_audio_drops(void);  // 音频帧丢弃累计(对端未知/发送失败)
uint32_t udp_audio_event_drops(void);  // 事件行丢弃累计
// 已学到的对端 IP 点分字符串;未学到返回 "0.0.0.0"(诊断/console 用)
const char *udp_audio_peer_ip(void);

#ifdef __cplusplus
}
#endif
