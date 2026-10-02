#pragma once

// 小智音频服务：ES8311 半双工录音 / 播放。
//
// 线程模型（这是修复闪退的核心）：
//   * websocket 任务只调用 PushIncomingAudio()：持锁几微秒写环形缓冲，绝不解码、
//     绝不碰 codec、绝不阻塞。
//   * 一条常驻工作线程 xzg_audio 承担"录音编码"和"播放解码"两种模式。两种模式
//     天然互斥（半双工），所以共用一份大栈就够了 —— Opus 编解码器都用栈上临时
//     缓冲，实测编码需 ~19KB、解码需 >6KB，在 ~90KB 堆上各开一条大栈根本放不下。
//   * Start*/Stop* 生命周期接口必须在同一条任务（控制任务）里调用；内部用递归锁
//     保护，且任何等待都有明确上界，永远不会无限期挂住调用者。
//
// 与旧实现的关键差异：
//   1. 旧 StopRecording() 用 `while (handle) vTaskDelay(10)` 无界自旋：录音任务一旦
//      卡在 portMAX_DELAY 的 I2S 读里，调用者就永久挂起，进而让
//      esp_websocket_client_stop() 的 portMAX_DELAY 等待也永不返回 → 看门狗复位。
//      现在所有等待都有上限，超时只记日志并继续收尾。
//   2. 旧实现从 websocket 任务里直接 StartPlayback()/StopRecording()，与录音任务、
//      按键任务并发操作同一个 codec 句柄（esp_codec_dev_open/close 非线程安全）
//      → 随机崩溃（真机已复现："AI 说话时按 OK 打断"必然重启）。现在 codec 生命周期
//      只在控制任务里发生。
//   3. 旧实现每帧都在网络任务里同步解码 + 写 I2S，直接阻塞网络收发。
//   4. 录音线程不再自己发网络包：mbedTLS 的 ssl_write 很吃栈，发送改由控制任务做，
//      既省栈，也让协议对象只被一条任务访问。

#include <cstdint>
#include <cstddef>
#include <functional>
#include "esp_err.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

namespace xiaozhi {

// 编码：60ms @ 16kHz 单声道 16bit（1920 字节 PCM → ~240 字节 Opus）
constexpr size_t kEncPcmFrameBytes = 1920;
constexpr int kEncFrameDurationMs = 60;

constexpr size_t kOpusFrameMaxBytes = 512;
// 解码：服务端下发 24kHz/60ms 单声道 PCM 为 2880 字节。
// 对齐官方 folo-ai-passport-xiaozhi，硬件原生 24kHz 输出，无需升采样。
constexpr size_t kMaxPcmFrameBytes = 2880;

class AudioService {
public:
    static AudioService &GetInstance();

    esp_err_t Init();
    // 应用退出时调用：停掉一切、销毁常驻工作线程、把缓冲还给堆。
    // 这样切到别的 App 时机器内存不会被小智占着（多 App 共存的前提）。
    void Deinit();

    // ---- websocket 任务调用：非阻塞，缓冲满则丢弃 ----
    void PushIncomingAudio(const uint8_t *opus_data, size_t len);

    // ---- 控制任务调用：录音生命周期 ----
    //
    // 编码器"提前 open、常驻复用"（2026-09-13 修复）：
    // 真机实测最容易拿到大块连续内存的时刻是 AudioService::Init()（此时
    // largest 约 30KB）；等到按下说话时堆已被 websocket/TLS 与 UI 打散，
    // largest 只剩 15-22KB，而 esp_opus_enc_open 需要约 25KB 连续块，
    // 必然失败（ESP_OPUS_ENC: Opus encoder init failed. ret:-7）。
    // 因此在 Init() 就把编码器开好常驻，StopRecording() 不再关它。
    // 只有在"播放需要内存、解码器开不出来"时才临时放掉，播完立刻补回。
    bool EnsureEncoder();

    bool StartRecording();
    void StopRecording();
    bool IsRecording() const { return is_recording_; }

    // ---- 控制任务调用：取出已编码的音频帧并发送 ----
    bool PopEncodedFrame(uint8_t *out, size_t out_cap, size_t *out_len);
    bool HasPendingEncoded() const;
    void ResetEncodedQueue();

    // ---- 控制任务调用：播放生命周期 ----
    void SetPlaybackSampleRate(int hz);
    int GetPlaybackSampleRate() const { return playback_rate_; }

    bool StartPlayback();
    void StopPlayback();    // 播完缓冲剩余音频再停（正常 tts stop）
    void AbortPlayback();   // 立刻丢弃缓冲并停止（打断 / 退出应用）
    bool IsPlaying() const { return is_playing_; }
    bool HasPendingPlayback() const;

    void Reset();

private:
    enum class Work : uint8_t { Idle, Record, Play };

    AudioService();
    ~AudioService();
    AudioService(const AudioService &) = delete;
    AudioService &operator=(const AudioService &) = delete;

    static void WorkerEntry(void *arg);
    void WorkerLoop();
    void RecordSection();
    void PlaySection();

    bool WaitWorkIdle(uint32_t timeout_ms, const char *name);
    bool OpenDecoder();

    // 通用字节环形缓冲（存 [u16 len][payload] 记录）
    struct Ring {
        uint8_t *buf;
        size_t cap;
        size_t head;
        size_t tail;
        size_t records;
        SemaphoreHandle_t lock;
        const char *name;
    };
    static void RingInit(Ring &r, uint8_t *buf, size_t cap, SemaphoreHandle_t lock,
                         const char *name);
    static size_t RingUsed(const Ring &r);
    static size_t RingFree(const Ring &r);
    static size_t RingCount(const Ring &r);
    static bool RingPush(Ring &r, const uint8_t *data, size_t len);
    static size_t RingPop(Ring &r, uint8_t *out, size_t out_cap);
    static void RingReset(Ring &r);

    void *opus_enc_handle_{nullptr};
    void *opus_dec_handle_{nullptr};
    int opus_dec_rate_{0};                  // 当前解码器使用的采样率
    bool enc_dropped_for_playback_{false};  // 播放时被迫放掉编码器，播完要补回

    volatile bool is_recording_{false};
    volatile bool is_playing_{false};
    volatile bool play_stop_{false};    // 硬停：立刻退出
    volatile bool play_drain_{false};   // 软停：播完缓冲再退出

    int playback_rate_{16000};

    uint8_t *playback_buf_{nullptr};    // Opus 入帧暂存
    uint8_t *record_pcm_buf_{nullptr};
    uint8_t *record_opus_buf_{nullptr};
    uint8_t *pcm_out_{nullptr};         // 解码输出（只在播放期间分配）

    TaskHandle_t worker_task_{nullptr};  // 常驻：录音/播放共用一条
    volatile Work work_{Work::Idle};
    volatile bool worker_quit_{false};
    SemaphoreHandle_t work_signal_{nullptr};
    SemaphoreHandle_t work_idle_{nullptr};

    SemaphoreHandle_t lifecycle_lock_{nullptr};

    // 播放环形缓冲 3072 ≈ 12~15 个 Opus 帧 ≈ 0.8 秒余量。配合短时 TCP 背压
    // (PushIncomingAudio 中 300ms 有界等待)可完全吸收服务端下发突发，且零丢帧。
    // 将容量从 8KB 收紧至 3KB，直接向系统堆归还 5120 字节内部 SRAM，确保
    // 录音栈与 Opus 编码器能顺利拿到连续内存。
    static constexpr size_t kPlayRingCap = 3072;
    uint8_t play_ring_buf_[kPlayRingCap];
    Ring play_ring_{};

    static constexpr size_t kTxRingCap = 512;
    uint8_t tx_ring_buf_[kTxRingCap];
    Ring tx_ring_{};
};

} // namespace xiaozhi
