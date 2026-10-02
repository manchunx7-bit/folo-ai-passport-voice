#include "apps/game/game_mic.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdatomic.h>
#include <string.h>

static const char *TAG = "game_mic";

#define SAMPLES 256 /* 16ms @16kHz;读块小,延迟低 */

/* 原始采样诊断(每 ~2s 一条,供声控阈值校准)。平时关掉:它会额外扫一遍
   256 个采样并打 UART 日志,属于纯调试开销。改 1 重新打开。 */
#define GAME_MIC_DEBUG_LOG 0

/* 丢掉 codec 上电瞬态的块数(10 × 16ms ≈ 160ms)。
   满量程垃圾曾把狗直接顶上天,不能省。 */
#define MIC_WARMUP_BLOCKS 10

/* EMA 系数:ema = ema*(4-1)/4 + mean/4 ≈ 48ms 时间常数。 */
#define MIC_EMA_SHIFT 2

static atomic_uint s_level;
static atomic_bool s_ready;
static atomic_bool s_active;
static TaskHandle_t s_task;

/* 等旧任务退出:最多 wait_ticks × 15ms。返回它是否已经消失。 */
static bool wait_task_exit(int wait_ticks) {
    for (int i = 0; i < wait_ticks && s_task; ++i) {
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    return s_task == NULL;
}

static void mic_task(void *arg) {
    (void)arg;
    int16_t pcm[SAMPLES]; /* 512B 在任务栈上,不占堆 */

    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_audio_init 失败: %s", esp_err_to_name(err));
        atomic_store(&s_active, false);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    bsp_audio_set_format(16000, 16, 1);

    uint32_t ema = 0;
    int warmup = MIC_WARMUP_BLOCKS;
#if GAME_MIC_DEBUG_LOG
    int dbg = 0;
#endif
    while (atomic_load(&s_active)) {
        if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        /* 均值整流:比 RMS 便宜(无乘法),对响度映射足够 */
        int32_t sum = 0;
#if GAME_MIC_DEBUG_LOG
        int32_t mn = 32767, mx = -32768;
#endif
        for (int i = 0; i < SAMPLES; ++i) {
            int32_t v = pcm[i];
            sum += v < 0 ? -v : v;
#if GAME_MIC_DEBUG_LOG
            if (v < mn) mn = v;
            if (v > mx) mx = v;
#endif
        }
#if GAME_MIC_DEBUG_LOG
        if (++dbg >= 120) { /* ~2s 一条,供声控阈值校准 */
            dbg = 0;
            int32_t mean_s = 0;
            for (int i = 0; i < SAMPLES; ++i) mean_s += pcm[i];
            ESP_LOGI(TAG, "raw: mean_abs=%ld mean_s=%ld min=%ld max=%ld",
                     (long)(sum / SAMPLES), (long)(mean_s / SAMPLES),
                     (long)mn, (long)mx);
        }
#endif
        const uint32_t mean = (uint32_t)(sum / SAMPLES);
        if (warmup) {
            if (--warmup == 0) atomic_store(&s_ready, true);
            continue;
        }
        /* EMA 全程无符号:ema*3/4 + mean/4。原来的 (mean-ema)/4 在 mean<ema 时
           发生无符号下溢,电平瞬间顶满 4095 并永远卡死 —— 已用真机抓到。
           首块直接播种(而不是从 0 慢慢爬),省掉约 64ms 的收敛,出声更跟手。 */
        ema = (ema == 0) ? mean : (ema * ((1u << MIC_EMA_SHIFT) - 1u) + mean) >> MIC_EMA_SHIFT;
        atomic_store(&s_level, (unsigned)(ema > 4095u ? 4095u : ema));
    }
    /* 不 deinit codec:bsp_audio 无关断 API,语音应用停止后同样保持打开,
       RX 空转功耗可忽略;App 互斥保证没有别人在用。 */
    ESP_LOGI(TAG, "mic task exit");
    s_task = NULL;
    vTaskDelete(NULL);
}

void game_mic_start(void) {
    /* 关键修复:原来这里只有 `if (s_task) return;`。退出 App 后立刻重进时旧任务
       往往还在 read 里(最多 16ms)没退出,s_task 非空 → 直接 return,于是
       这一局静默地没有麦克风、完全不可玩。现在改为"先请它退出并等它"。 */
    if (s_task) {
        atomic_store(&s_active, false);
        if (!wait_task_exit(40)) { /* 最多 600ms */
            ESP_LOGW(TAG, "旧采音任务未退出,放弃本次启动");
            return;
        }
    }
    /* 清掉上一局残留的电平/就绪位,避免进场第一帧就带着旧响度起飞。 */
    atomic_store(&s_level, 0);
    atomic_store(&s_ready, false);
    atomic_store(&s_active, true);
    if (xTaskCreate(mic_task, "game_mic", 3072, NULL, 3, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "mic task create failed");
        atomic_store(&s_active, false);
        s_task = NULL;
    }
}

void game_mic_stop(void) {
    if (!s_task) return;
    atomic_store(&s_active, false);
    if (!wait_task_exit(40)) { /* 最多等 600ms */
        ESP_LOGW(TAG, "mic task slow to exit, leaving it to self-delete");
    }
}

uint16_t game_mic_level(void) { return (uint16_t)atomic_load(&s_level); }

bool game_mic_ready(void) { return atomic_load(&s_ready); }
