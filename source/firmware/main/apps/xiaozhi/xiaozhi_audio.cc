#include "apps/xiaozhi/xiaozhi_audio.h"
#include "bsp_audio.h"
#include "encoder/impl/esp_opus_enc.h"
#include "decoder/impl/esp_opus_dec.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <cstring>
#include <cstdlib>

static const char *TAG = "xiaozhi_audio";

namespace xiaozhi {

namespace {
constexpr int kEncodeRate = 16000;      // 编码固定 16kHz（与官方固件一致）
constexpr int kChannels = 1;
constexpr int kBitsPerSample = 16;

// 有界等待上界：卡住了也必须能收尾，这些是兜底而非期望路径。
constexpr uint32_t kWorkIdleTimeoutMs = 1500;
constexpr uint32_t kPlaybackDrainTimeoutMs = 2000;
constexpr uint32_t kPlaybackAbortTimeoutMs = 800;
constexpr uint32_t kPlaybackEnqueueWaitMs = 300;

// 小智的 TTS 包由 WebSocket 突发下发。没有预缓冲时，网络任务稍有抖动，60ms
// PCM 播完后 I2S 就会短暂断粮，听起来像连续的电流爆音。先积累 3 帧（约 180ms）
// 再开播；若中途耗尽，也重新积累，宁可有很短的停顿，不把断续噪声送进功放。
constexpr size_t kPlaybackPrebufferFrames = 3;

// TTS follows the user's master volume without a hidden 70% ceiling. Recording
// and idle paths still request 0%, which the BSP now applies as a real mute.
constexpr uint8_t kXiaozhiPlaybackVolume = 100;

constexpr size_t kRingHdr = 2;          // 每条记录 2 字节长度前缀

// 开编码器前要求的最小连续空闲块。Opus 编码器约 25KB 的内部状态是按块分配的，
// 连续块太小就意味着 open 会"部分成功"，随后在 process 里写空指针。
// 真机实测（2026-09-13）：largest=22528 / 23552 时 open 失败
// （ESP_OPUS_ENC: Opus encoder init failed. ret:-7），largest=32768 时成功，
// 成功瞬间 largest 掉到 7680 —— 也就是说它一次要吃掉约 25KB 连续块。
// 门槛取 20KB：真正没救的情况（<20KB）拦得住，剩下的交给
// esp_opus_enc_open 的返回值 —— 那个是权威判据。
constexpr size_t kMinLargestBlockForEncode = 20 * 1024;

// 常驻工作线程栈。Opus 编解码器的临时缓冲开在栈上：编码（clt_mdct_forward_c /
// silk_NSQ_c）实测需 ~19KB，解码（quant_all_bands）>6KB。20KB 版本在真机连续
// 对话中最低只剩约 0.8KB，22KB 即可保留约 2.8KB 的安全余量，同时减少连续块申请门槛。
// 关键：这个线程要尽早创建 —— 那时堆新鲜连续，22KB 连续块一定拿得到；等按下
// 说话才申请，堆已被 TLS/OTA 打散（真机实测 largest 只剩 15-22KB），必然失败。
// 编码器同理：见 EnsureEncoder()，它现在在 Init() 里就常驻打开。
constexpr size_t kWorkerStack = 22 * 1024;
} // namespace

// ---------------------------------------------------------------- ring buffer

void AudioService::RingInit(Ring &r, uint8_t *buf, size_t cap, SemaphoreHandle_t lock,
                            const char *name) {
    r.buf = buf;
    r.cap = cap;
    r.head = 0;
    r.tail = 0;
    r.records = 0;
    r.lock = lock;
    r.name = name;
}

size_t AudioService::RingUsed(const Ring &r) {
    return (r.head + r.cap - r.tail) % r.cap;
}

size_t AudioService::RingFree(const Ring &r) {
    return r.cap - RingUsed(r) - 1;   // 留 1 字节区分满/空
}

size_t AudioService::RingCount(const Ring &r) {
    size_t count = 0;
    if (xSemaphoreTake(r.lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = r.records;
        xSemaphoreGive(r.lock);
    }
    return count;
}

bool AudioService::RingPush(Ring &r, const uint8_t *data, size_t len) {
    if (len == 0 || len > 0xFFFF) return false;

    bool ok = false;
    if (xSemaphoreTake(r.lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        const size_t need = kRingHdr + len;
        if (RingFree(r) >= need) {
            if (r.head + need <= r.cap) {
                r.buf[r.head] = (uint8_t)(len & 0xFF);
                r.buf[r.head + 1] = (uint8_t)((len >> 8) & 0xFF);
                std::memcpy(r.buf + r.head + kRingHdr, data, len);
            } else {
                for (size_t i = 0; i < need; ++i) {
                    uint8_t byte;
                    if (i == 0) byte = (uint8_t)(len & 0xFF);
                    else if (i == 1) byte = (uint8_t)((len >> 8) & 0xFF);
                    else byte = data[i - kRingHdr];
                    r.buf[(r.head + i) % r.cap] = byte;
                }
            }
            r.head = (r.head + need) % r.cap;
            r.records++;
            ok = true;
        }
        xSemaphoreGive(r.lock);
    }
    return ok;
}

size_t AudioService::RingPop(Ring &r, uint8_t *out, size_t out_cap) {
    size_t result = 0;
    if (xSemaphoreTake(r.lock, pdMS_TO_TICKS(20)) != pdTRUE) return 0;

    if (RingUsed(r) >= kRingHdr) {
        const size_t h = r.tail;
        const size_t len = (size_t)r.buf[h] | ((size_t)r.buf[(h + 1) % r.cap] << 8);
        if (len > 0 && len <= out_cap && RingUsed(r) >= kRingHdr + len) {
            for (size_t i = 0; i < len; ++i) {
                out[i] = r.buf[(h + kRingHdr + i) % r.cap];
            }
            r.tail = (h + kRingHdr + len) % r.cap;
            if (r.records > 0) r.records--;
            result = len;
        } else if (len > out_cap) {
            ESP_LOGW(TAG, "%s: record len=%u > cap=%u, dropped", r.name, (unsigned)len,
                     (unsigned)out_cap);
            r.tail = (h + kRingHdr + len) % r.cap;
            if (r.records > 0) r.records--;
        }
    }
    xSemaphoreGive(r.lock);
    return result;
}

void AudioService::RingReset(Ring &r) {
    if (r.lock == nullptr) return;
    if (xSemaphoreTake(r.lock, portMAX_DELAY) == pdTRUE) {
        r.head = 0;
        r.tail = 0;
        r.records = 0;
        xSemaphoreGive(r.lock);
    }
}

// ------------------------------------------------------------------- lifecycle

AudioService &AudioService::GetInstance() {
    static AudioService s_instance;
    return s_instance;
}

AudioService::AudioService() {
    lifecycle_lock_ = xSemaphoreCreateRecursiveMutex();
    RingInit(play_ring_, play_ring_buf_, kPlayRingCap, xSemaphoreCreateMutex(), "play_ring");
    RingInit(tx_ring_, tx_ring_buf_, kTxRingCap, xSemaphoreCreateMutex(), "tx_ring");
}

AudioService::~AudioService() {
    if (opus_enc_handle_) esp_opus_enc_close(opus_enc_handle_);
    if (opus_dec_handle_) esp_opus_dec_close(opus_dec_handle_);
}

esp_err_t AudioService::Init() {
    if (lifecycle_lock_ == nullptr) return ESP_ERR_NO_MEM;

    esp_err_t ret = bsp_audio_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_audio_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    bsp_audio_set_format(kEncodeRate, kBitsPerSample, kChannels);
    bsp_audio_set_volume(0);  // 录音/待机时关闭扬声器输出，避免功放底噪串入麦克风

    if (record_pcm_buf_ == nullptr) {
        record_pcm_buf_ = static_cast<uint8_t *>(malloc(kEncPcmFrameBytes));
    }
    if (record_opus_buf_ == nullptr) {
        record_opus_buf_ = static_cast<uint8_t *>(malloc(kOpusFrameMaxBytes));
    }
    if (playback_buf_ == nullptr) {
        playback_buf_ = static_cast<uint8_t *>(malloc(kOpusFrameMaxBytes));
    }
    if (!record_pcm_buf_ || !record_opus_buf_ || !playback_buf_) {
        ESP_LOGE(TAG, "Audio buffers allocation failed (free=%u largest=%u)",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return ESP_ERR_NO_MEM;
    }

    RingReset(play_ring_);
    RingReset(tx_ring_);

    if (worker_task_ == nullptr) {
        if (work_signal_ == nullptr) work_signal_ = xSemaphoreCreateBinary();
        if (work_idle_ == nullptr) work_idle_ = xSemaphoreCreateBinary();
        if (work_signal_ == nullptr || work_idle_ == nullptr) {
            ESP_LOGE(TAG, "Audio sync primitives failed");
            return ESP_ERR_NO_MEM;
        }
        while (xSemaphoreTake(work_signal_, 0) == pdTRUE) { }
        while (xSemaphoreTake(work_idle_, 0) == pdTRUE) { }

        worker_quit_ = false;
        work_ = Work::Idle;
        if (xTaskCreate(WorkerEntry, "xzg_audio", kWorkerStack, this, 5, &worker_task_) != pdPASS) {
            ESP_LOGE(TAG, "Audio worker create failed (free=%u largest=%u)",
                     (unsigned)esp_get_free_heap_size(),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            worker_task_ = nullptr;
            return ESP_ERR_NO_MEM;
        }
    }

    // 编码器在这里就开好常驻：此刻是整个会话里连续内存最完整的时刻。
    // 失败不致命 —— 播放/TTS 仍然可用，StartRecording() 会再试一次。
    if (!EnsureEncoder()) {
        ESP_LOGW(TAG, "Encoder not pre-opened; will retry when recording starts");
    }

    ESP_LOGI(TAG, "Audio service ready. free=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return ESP_OK;
}

void AudioService::Deinit() {
    StopRecording();
    AbortPlayback();

    if (worker_task_ != nullptr) {
        worker_quit_ = true;
        work_ = Work::Idle;
        xSemaphoreGive(work_signal_);
        const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
        while (worker_task_ != nullptr && xTaskGetTickCount() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (worker_task_ != nullptr) {
            ESP_LOGE(TAG, "Audio worker did not exit; leaking its handle on purpose");
            worker_task_ = nullptr;
        }
    }

    // 编解码器必须在这里显式关掉：编码器现在是常驻的（StopRecording 不再关），
    // 不关就会一直占着 ~25KB，退出小智后别的 App 也拿不回来。
    if (opus_enc_handle_ != nullptr) {
        esp_opus_enc_close(opus_enc_handle_);
        opus_enc_handle_ = nullptr;
    }
    if (opus_dec_handle_ != nullptr) {
        esp_opus_dec_close(opus_dec_handle_);
        opus_dec_handle_ = nullptr;
        opus_dec_rate_ = 0;
    }
    enc_dropped_for_playback_ = false;

    if (record_pcm_buf_) { free(record_pcm_buf_); record_pcm_buf_ = nullptr; }
    if (record_opus_buf_) { free(record_opus_buf_); record_opus_buf_ = nullptr; }
    if (playback_buf_) { free(playback_buf_); playback_buf_ = nullptr; }
    if (pcm_out_) { free(pcm_out_); pcm_out_ = nullptr; }

    RingReset(play_ring_);
    RingReset(tx_ring_);
    ESP_LOGI(TAG, "Audio service deinit. free=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

bool AudioService::WaitWorkIdle(uint32_t timeout_ms, const char *name) {
    if (worker_task_ == nullptr) return true;
    const bool ok = xSemaphoreTake(work_idle_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    if (!ok) ESP_LOGE(TAG, "%s: audio worker did not report idle in %ums", name, timeout_ms);
    return ok;
}

// ------------------------------------------------------------------- recording

bool AudioService::EnsureEncoder() {
    if (opus_enc_handle_ != nullptr) return true;

    // 内存闸门：与其让编码器内部 malloc 失败后在 process 里写空指针崩掉，
    // 不如提前拒绝并让 UI 明确告诉用户。真正的权威判据是 open 的返回值。
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (largest < kMinLargestBlockForEncode) {
        ESP_LOGE(TAG, "Refusing to open encoder: largest free block %u < %u (free=%u)",
                 (unsigned)largest, (unsigned)kMinLargestBlockForEncode,
                 (unsigned)esp_get_free_heap_size());
        return false;
    }

    // CELT(APPLICATION_AUDIO)：与官方 xiaozhi-esp32 语音链路一致，也是改造前能在
    // 官方后台跑通的配置（SILK/20ms 的尝试会导致服务端完全不回应，已回退）。
    esp_opus_enc_config_t enc_cfg = ESP_OPUS_ENC_CONFIG_DEFAULT();
    enc_cfg.sample_rate = kEncodeRate;
    enc_cfg.channel = ESP_AUDIO_MONO;
    enc_cfg.bits_per_sample = kBitsPerSample;
    enc_cfg.bitrate = 32000;
    enc_cfg.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    enc_cfg.application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO;
    enc_cfg.complexity = 0;      // C3 单核 160MHz，复杂度 0 保实时
    enc_cfg.enable_fec = false;
    // DTX 关掉：按下说话时静音抑制只有坏处 —— 一旦判定静音就一个包都不发，
    // 服务端收不到音频干脆不回应，表现出来正是"对话之后没有返回"。
    enc_cfg.enable_dtx = false;
    enc_cfg.enable_vbr = true;

    const esp_audio_err_t err = esp_opus_enc_open(&enc_cfg, sizeof(enc_cfg), &opus_enc_handle_);
    if (err != ESP_AUDIO_ERR_OK || opus_enc_handle_ == nullptr) {
        ESP_LOGE(TAG, "Opus encoder open failed: %d (free=%u largest=%u)", (int)err,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        opus_enc_handle_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "Opus encoder resident. free=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return true;
}

bool AudioService::StartRecording() {
    xSemaphoreTakeRecursive(lifecycle_lock_, portMAX_DELAY);

    if (is_recording_) {
        xSemaphoreGiveRecursive(lifecycle_lock_);
        return true;
    }

    // 半双工：录音前必须停掉播放（lifecycle_lock_ 是递归锁，同任务直接调用安全）
    if (is_playing_ || work_ == Work::Play) {
        AbortPlayback();
    }

    if (record_pcm_buf_ == nullptr || record_opus_buf_ == nullptr) {
        ESP_LOGE(TAG, "Record buffers missing; Init() not done?");
        xSemaphoreGiveRecursive(lifecycle_lock_);
        return false;
    }
    if (worker_task_ == nullptr) {
        ESP_LOGE(TAG, "Audio worker not running; Init() failed earlier?");
        xSemaphoreGiveRecursive(lifecycle_lock_);
        return false;
    }

    RingReset(tx_ring_);
    bsp_audio_set_format(kEncodeRate, kBitsPerSample, kChannels);
    bsp_audio_set_volume(0);

    // 编码器一般在 Init() 就已经开好常驻；这里只在"上次播放时被迫放掉、
    // 且播完没能补回来"的兜底情况下才真的分配。
    if (!EnsureEncoder()) {
        ESP_LOGE(TAG, "StartRecording: encoder unavailable (free=%u largest=%u)",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        xSemaphoreGiveRecursive(lifecycle_lock_);
        return false;
    }

    while (xSemaphoreTake(work_idle_, 0) == pdTRUE) { }
    is_recording_ = true;
    work_ = Work::Record;
    xSemaphoreGive(work_signal_);

    ESP_LOGI(TAG, "Recording started. free=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    xSemaphoreGiveRecursive(lifecycle_lock_);
    return true;
}

void AudioService::StopRecording() {
    xSemaphoreTakeRecursive(lifecycle_lock_, portMAX_DELAY);

    if (!is_recording_ && opus_enc_handle_ == nullptr) {
        xSemaphoreGiveRecursive(lifecycle_lock_);
        return;
    }

    is_recording_ = false;                 // 录音循环的唯一退出条件
    const bool idle = (work_ != Work::Record) || WaitWorkIdle(kWorkIdleTimeoutMs, "StopRecording");
    if (work_ == Work::Record) work_ = Work::Idle;

    // 编码器刻意不关：它现在是常驻的（见 EnsureEncoder 的说明）。
    // 关掉它等于把 25KB 连续块还给堆，等下一次按下说话时堆已经碎得开不出来了。
    // 真正释放发生在 Deinit()（退出小智）或 StartPlayback() 的内存救急路径。
    // 用户已经停止说话，队列里剩下的半帧没必要再发
    RingReset(tx_ring_);

    if (worker_task_ != nullptr) {
        ESP_LOGI(TAG, "Recording stopped (idle=%d, worker stack free %u bytes). free=%u", (int)idle,
                 (unsigned)(uxTaskGetStackHighWaterMark(worker_task_) * sizeof(StackType_t)),
                 (unsigned)esp_get_free_heap_size());
    } else {
        ESP_LOGI(TAG, "Recording stopped. free=%u", (unsigned)esp_get_free_heap_size());
    }
    xSemaphoreGiveRecursive(lifecycle_lock_);
}

void AudioService::RecordSection() {
    while (is_recording_ && opus_enc_handle_ != nullptr) {
        // bsp_audio_read 是 portMAX_DELAY 阻塞读（见 bsp_audio.h）；正常 60ms 一块
        // 必然返回，因此 is_recording_ 的检查点足够密集。
        if (bsp_audio_read(record_pcm_buf_, kEncPcmFrameBytes) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (!is_recording_ || opus_enc_handle_ == nullptr) break;

        esp_audio_enc_in_frame_t in_f = {};
        in_f.buffer = record_pcm_buf_;
        in_f.len = (uint32_t)kEncPcmFrameBytes;

        esp_audio_enc_out_frame_t out_f = {};
        out_f.buffer = record_opus_buf_;
        out_f.len = (uint32_t)kOpusFrameMaxBytes;
        out_f.encoded_bytes = 0;

        const esp_audio_err_t res = esp_opus_enc_process(opus_enc_handle_, &in_f, &out_f);
        if (res == ESP_AUDIO_ERR_OK && out_f.encoded_bytes > 0) {
            // 只入队，不发送：网络 IO 交给控制任务
            if (!RingPush(tx_ring_, record_opus_buf_, out_f.encoded_bytes)) {
                ESP_LOGW(TAG, "TX ring full, dropping a frame");
            }
        }
    }
}

bool AudioService::PopEncodedFrame(uint8_t *out, size_t out_cap, size_t *out_len) {
    const size_t n = RingPop(tx_ring_, out, out_cap);
    if (out_len) *out_len = n;
    return n > 0;
}

bool AudioService::HasPendingEncoded() const {
    return RingUsed(tx_ring_) > 0;
}

void AudioService::ResetEncodedQueue() {
    RingReset(tx_ring_);
}

// -------------------------------------------------------------------- playback

void AudioService::SetPlaybackSampleRate(int hz) {
    if (hz < 8000 || hz > 48000) {
        ESP_LOGW(TAG, "Ignoring odd playback sample rate %d", hz);
        return;
    }
    if (hz != playback_rate_) {
        ESP_LOGI(TAG, "Playback sample rate -> %d Hz", hz);
        playback_rate_ = hz;
    }
}

bool AudioService::OpenDecoder() {
    esp_opus_dec_cfg_t dec_cfg = ESP_OPUS_DEC_CONFIG_DEFAULT();
    // 官方服务端 hello 实测下发 sample_rate=24000；解码器必须按它配置，
    // 否则 24kHz 的 TTS 会被当 16kHz 解出来，音调/语速全错。
    dec_cfg.sample_rate = (uint32_t)playback_rate_;
    dec_cfg.channel = ESP_AUDIO_MONO;
    dec_cfg.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS;

    void *handle = nullptr;
    const esp_audio_err_t err = esp_opus_dec_open(&dec_cfg, sizeof(dec_cfg), &handle);
    if (err != ESP_AUDIO_ERR_OK || handle == nullptr) {
        ESP_LOGW(TAG, "Opus decoder open failed: %d (free=%u largest=%u)", (int)err,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return false;
    }
    opus_dec_handle_ = handle;
    opus_dec_rate_ = playback_rate_;
    return true;
}

bool AudioService::StartPlayback() {
    xSemaphoreTakeRecursive(lifecycle_lock_, portMAX_DELAY);

    if (is_playing_ && work_ == Work::Play) {
        xSemaphoreGiveRecursive(lifecycle_lock_);
        return true;
    }

    // 半双工：播放前必须停掉录音（递归锁，同任务直接调用安全）。
    // 注意：编码器是常驻的，不能因为它存在就走 StopRecording() —— 那会白白
    // 重置一次发送队列。
    if (is_recording_) {
        StopRecording();
    }

    if (playback_buf_ == nullptr) {
        ESP_LOGE(TAG, "Playback buffer missing; Init() not done?");
        xSemaphoreGiveRecursive(lifecycle_lock_);
        return false;
    }
    if (worker_task_ == nullptr) {
        ESP_LOGE(TAG, "Audio worker not running");
        xSemaphoreGiveRecursive(lifecycle_lock_);
        return false;
    }

    if (pcm_out_ == nullptr) {
        pcm_out_ = static_cast<uint8_t *>(malloc(kMaxPcmFrameBytes));
        if (pcm_out_ == nullptr) {
            ESP_LOGE(TAG, "PCM out buffer alloc failed (largest=%u)",
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            xSemaphoreGiveRecursive(lifecycle_lock_);
            return false;
        }
    }

    // 服务端重新下发过采样率就换掉旧解码器（此时录音已停、worker 空闲）
    if (opus_dec_handle_ != nullptr && opus_dec_rate_ != playback_rate_) {
        ESP_LOGI(TAG, "Decoder sample rate %d -> %d; reopening", opus_dec_rate_, playback_rate_);
        esp_opus_dec_close(opus_dec_handle_);
        opus_dec_handle_ = nullptr;
        opus_dec_rate_ = 0;
    }

    if (opus_dec_handle_ == nullptr && !OpenDecoder()) {
        // 常驻编码器占着约 25KB，解码器可能因此开不出来。此时临时把它放掉：
        // 反正播放期间也用不到录音，播完 StopPlayback() 会立刻补回来。
        if (opus_enc_handle_ != nullptr) {
            ESP_LOGW(TAG, "Dropping resident encoder to make room for decoder (free=%u largest=%u)",
                     (unsigned)esp_get_free_heap_size(),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            esp_opus_enc_close(opus_enc_handle_);
            opus_enc_handle_ = nullptr;
            enc_dropped_for_playback_ = true;
        }
        if (!OpenDecoder()) {
            ESP_LOGE(TAG, "Opus decoder open failed (free=%u largest=%u)",
                     (unsigned)esp_get_free_heap_size(),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            xSemaphoreGiveRecursive(lifecycle_lock_);
            return false;
        }
    }

    // 严格半双工：走到这里时 StopRecording() 已经等待录音 worker 退出并关闭编码器；
    // 随后才把 codec 从 16kHz 录音切换为服务端的播放采样率。
    // 先静音再切格式，避免 codec close/open 的瞬态进入功放。每个 App 仍必须声明
    // 自己的逻辑音量；播放音量由整机设置控制，不再叠加隐藏的 70% 上限。
    bsp_audio_set_volume(0);
    // 对齐官方 folo-ai-passport-xiaozhi: 直接以原生采样率（24000Hz）驱动 ES8311，
    // 不再做 48kHz 升采样，消除时钟与插值失真，同时释放 2.9KB 堆内存。
    if (bsp_audio_set_format((uint32_t)playback_rate_, kBitsPerSample, kChannels) != ESP_OK) {
        ESP_LOGE(TAG, "Playback codec format switch failed");
        esp_opus_dec_close(opus_dec_handle_);
        opus_dec_handle_ = nullptr;
        opus_dec_rate_ = 0;
        xSemaphoreGiveRecursive(lifecycle_lock_);
        return false;
    }
    bsp_audio_set_volume(kXiaozhiPlaybackVolume);

    while (xSemaphoreTake(work_idle_, 0) == pdTRUE) { }
    work_ = Work::Play;
    play_stop_ = false;
    play_drain_ = false;
    is_playing_ = true;
    xSemaphoreGive(work_signal_);

    ESP_LOGI(TAG, "Playback started: Opus %dHz (native). free=%u", playback_rate_,
             (unsigned)esp_get_free_heap_size());
    xSemaphoreGiveRecursive(lifecycle_lock_);
    return true;
}

void AudioService::StopPlayback() {
    xSemaphoreTakeRecursive(lifecycle_lock_, portMAX_DELAY);
    if (work_ != Work::Play && opus_dec_handle_ == nullptr) {
        is_playing_ = false;
        xSemaphoreGiveRecursive(lifecycle_lock_);
        return;
    }

    // 软停：播完缓冲里剩余音频再退出（正常 tts stop，尾音不被切断）。
    // 这里刻意不把 is_playing_ 置 false —— 播放循环用它决定是否继续写 codec。
    play_drain_ = true;
    xSemaphoreGive(work_signal_);

    bool idle = WaitWorkIdle(kPlaybackDrainTimeoutMs, "StopPlayback");
    if (!idle) {
        play_stop_ = true;
        xSemaphoreGive(work_signal_);
        idle = WaitWorkIdle(kPlaybackAbortTimeoutMs, "StopPlayback(force)");
    }
    work_ = Work::Idle;
    is_playing_ = false;
    bsp_audio_set_volume(0);
    RingReset(play_ring_);

    if (opus_dec_handle_ != nullptr) {
        if (idle) {
            esp_opus_dec_close(opus_dec_handle_);
            opus_dec_handle_ = nullptr;
            opus_dec_rate_ = 0;
        } else {
            // 未确认 worker 停止时刻意保留句柄：它还可能被解码循环用到，
            // 置空等于泄漏（旧代码正是这么丢的）。下次 StartPlayback 会复用。
            ESP_LOGW(TAG, "Decoder busy; keep handle, close it later");
        }
    }
    if (idle && pcm_out_ != nullptr) {
        free(pcm_out_);
        pcm_out_ = nullptr;
    }

    // 播放期间为腾内存放掉过编码器 → 这里刚释放完解码器，是补回它的最好时机
    if (enc_dropped_for_playback_) {
        enc_dropped_for_playback_ = false;
        EnsureEncoder();
    }

    ESP_LOGI(TAG, "Playback stopped (drained=%d). free=%u", (int)idle,
             (unsigned)esp_get_free_heap_size());
    xSemaphoreGiveRecursive(lifecycle_lock_);
}

void AudioService::AbortPlayback() {
    xSemaphoreTakeRecursive(lifecycle_lock_, portMAX_DELAY);
    if (work_ != Work::Play && opus_dec_handle_ == nullptr) {
        is_playing_ = false;
        RingReset(play_ring_);
        if (enc_dropped_for_playback_) {
            enc_dropped_for_playback_ = false;
            EnsureEncoder();
        }
        xSemaphoreGiveRecursive(lifecycle_lock_);
        return;
    }

    // 上次收尾时 worker 没确认空闲、解码器句柄被保留下来了：这里 worker 已不在
    // 播放态，短暂等一下确认它真的停了就补关掉，避免这份内存一直挂着。
    if (work_ != Work::Play && opus_dec_handle_ != nullptr &&
        xSemaphoreTake(work_idle_, pdMS_TO_TICKS(50)) == pdTRUE) {
        esp_opus_dec_close(opus_dec_handle_);
        opus_dec_handle_ = nullptr;
        opus_dec_rate_ = 0;
        is_playing_ = false;
        RingReset(play_ring_);
        if (enc_dropped_for_playback_) {
            enc_dropped_for_playback_ = false;
            EnsureEncoder();
        }
        xSemaphoreGiveRecursive(lifecycle_lock_);
        return;
    }

    is_playing_ = false;
    play_stop_ = true;          // 立刻退出，不等缓冲
    play_drain_ = false;
    bsp_audio_set_volume(0);
    RingReset(play_ring_);
    xSemaphoreGive(work_signal_);

    const bool idle = WaitWorkIdle(kPlaybackAbortTimeoutMs, "AbortPlayback");
    work_ = Work::Idle;
    if (opus_dec_handle_ != nullptr) {
        if (idle) {
            esp_opus_dec_close(opus_dec_handle_);
            opus_dec_handle_ = nullptr;
            opus_dec_rate_ = 0;
        } else {
            ESP_LOGW(TAG, "Decoder busy; keep handle, close it later");
        }
    }
    if (idle && pcm_out_ != nullptr) {
        free(pcm_out_);
        pcm_out_ = nullptr;
    }

    // 同 StopPlayback：被打断时也要把为腾内存放掉的编码器补回来
    if (enc_dropped_for_playback_) {
        enc_dropped_for_playback_ = false;
        EnsureEncoder();
    }

    ESP_LOGI(TAG, "Playback aborted (idle=%d). free=%u", (int)idle,
             (unsigned)esp_get_free_heap_size());
    xSemaphoreGiveRecursive(lifecycle_lock_);
}

void AudioService::PlaySection() {
    bool primed = false;
    bool format_logged = false;
    uint32_t starve_ticks = 0;
    while (!play_stop_) {
        if (!primed) {
            const size_t queued = RingCount(play_ring_);
            if (queued == 0 && play_drain_) break;
            if (!play_drain_ && queued < kPlaybackPrebufferFrames) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            primed = queued > 0;
            if (primed) {
                ESP_LOGI(TAG, "Playback buffer primed with %u Opus frames",
                         (unsigned)queued);
            }
        }

        const size_t got = RingPop(play_ring_, playback_buf_, kOpusFrameMaxBytes);
        if (got == 0) {
            // 收到软停且缓冲已排空 → 正常收尾（尾音已全部写进 codec）
            if (play_drain_ && !HasPendingPlayback()) break;
            vTaskDelay(pdMS_TO_TICKS(5));
            starve_ticks += 5;
            // 仅当严重断流（超过 200ms 无任何数据）时才重新标记未预缓冲，
            // 避免日常网络微抖动反复重置预缓冲，导致底层 DMA 播空而产生爆音。
            if (starve_ticks >= 200) {
                primed = false;
                starve_ticks = 0;
            }
            continue;
        }
        starve_ticks = 0;

        esp_audio_dec_in_raw_t in_raw = {};
        in_raw.buffer = playback_buf_;
        in_raw.len = (uint32_t)got;

        esp_audio_dec_out_frame_t out_frame = {};
        out_frame.buffer = pcm_out_;
        out_frame.len = (uint32_t)kMaxPcmFrameBytes;

        esp_audio_dec_info_t dec_info = {};
        const esp_audio_err_t err =
            esp_opus_dec_decode(opus_dec_handle_, &in_raw, &out_frame, &dec_info);
        if (err == ESP_AUDIO_ERR_OK && out_frame.decoded_size > 0 && !play_stop_) {
            if (!format_logged) {
                ESP_LOGI(TAG, "Decoded PCM frame=%u bytes, I2S direct write",
                         (unsigned)out_frame.decoded_size);
                format_logged = true;
            }
            if (bsp_audio_write(out_frame.buffer, out_frame.decoded_size) != ESP_OK) {
                ESP_LOGE(TAG, "I2S playback write failed (%u bytes)",
                         (unsigned)out_frame.decoded_size);
                break;
            }
        } else if (err != ESP_AUDIO_ERR_OK) {
            ESP_LOGW(TAG, "Opus decode failed: %d (%u-byte frame)", (int)err, (unsigned)got);
        }
    }
}

void AudioService::PushIncomingAudio(const uint8_t *opus_data, size_t len) {
    if (opus_data == nullptr || len == 0) return;

    // TTS 服务端可能以高于实时播放的速度突发下发。旧逻辑 2KB 队列一满就直接
    // 丢 Opus 帧，真机日志已确认长回复中连续丢包，听感正是滋滋/爆裂声。
    // 现在先用更大的队列吸收突发；仍满时在 websocket 接收任务上短暂等待，利用
    // TCP 背压让发送端降速。300ms 小于保活/协议超时量级，也不会长期卡住回调。
    const TickType_t wait_started = xTaskGetTickCount();
    const TickType_t wait_ticks = pdMS_TO_TICKS(kPlaybackEnqueueWaitMs);
    while (!RingPush(play_ring_, opus_data, len)) {
        if (play_stop_ || xTaskGetTickCount() - wait_started >= wait_ticks) {
            ESP_LOGW(TAG, "Playback ring full after %ums wait (%u/%u), frame dropped",
                     (unsigned)kPlaybackEnqueueWaitMs, (unsigned)RingUsed(play_ring_),
                     (unsigned)kPlayRingCap);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    // 立刻唤醒工作线程（信号量无句柄，天然无"刚读到句柄任务就自删"竞态）
    if (work_signal_ != nullptr) xSemaphoreGive(work_signal_);
}

bool AudioService::HasPendingPlayback() const {
    return RingUsed(play_ring_) > 0;
}

void AudioService::WorkerEntry(void *arg) {
    static_cast<AudioService *>(arg)->WorkerLoop();
}

// 常驻：平时停在 work_signal_ 上不占 CPU，收到信号才按模式干活。
void AudioService::WorkerLoop() {
    ESP_LOGI(TAG, "Audio worker running (stack %u bytes)", (unsigned)kWorkerStack);

    while (!worker_quit_) {
        if (xSemaphoreTake(work_signal_, pdMS_TO_TICKS(500)) != pdTRUE) continue;
        if (worker_quit_) break;

        if (work_ == Work::Record) {
            RecordSection();
        } else if (work_ == Work::Play) {
            PlaySection();
        }
        // 无论哪种模式结束，都回报"已停"，让控制任务安全地关闭句柄
        xSemaphoreGive(work_idle_);
    }

    ESP_LOGI(TAG, "Audio worker exiting (stack free %u bytes)",
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    worker_task_ = nullptr;
    vTaskDelete(NULL);
}

void AudioService::Reset() {
    StopRecording();
    AbortPlayback();
}

} // namespace xiaozhi
