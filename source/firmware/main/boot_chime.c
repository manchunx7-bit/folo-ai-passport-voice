// main/boot_chime.c —— 复古开机音:方波琶音 E5-G5-B5-E6(约 0.55s)。
// 合成参数是"手感"调的:每音 60% 线性衰减 + 15ms 静音分音,典型 8-bit 开机味。
// 播放走专用短命任务;结束时 bsp_audio_deinit() 归还全部音频内存。
#include "boot_chime.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "boot_chime";

#define CHIME_HZ     16000          // 与语音/游戏应用的默认格式一致
#define CHIME_AMP    7000           // ~21% 满幅:主音量 80% 下足够清晰不刺耳
#define NOTE_BUF_N   1600           // 100ms @16kHz(任务栈放不下,临时堆分配)

// 2026-09-13:原为 3.2KB .bss 常驻。开机铃只响 0.55s,却永久占着内部 DRAM ——
// 而 C3 堆里这块连续内存正是小智 Opus 编码器(~25KB 连续块)能不能开出来的
// 关键。改为 chime 任务内 malloc、用完即 free。

// 方波 + 线性衰减包络(衰到 40%):比恒幅方波更" electronic start"而不是警报
static void synth_square(int16_t *note, int samples, int hz) {
    const float step = (float)hz / CHIME_HZ;
    float phase = 0.0f;
    for (int i = 0; i < samples; ++i) {
        phase += step;
        if (phase >= 1.0f) phase -= 1.0f;
        const float env = 1.0f - 0.6f * (float)i / samples;
        note[i] = (int16_t)((phase < 0.5f ? CHIME_AMP : -CHIME_AMP) * env);
    }
}

static void play_note(int16_t *note, int hz, int ms) {
    const int total = CHIME_HZ * ms / 1000;
    int done = 0;
    while (done < total) {
        const int n = (total - done > NOTE_BUF_N) ? NOTE_BUF_N : total - done;
        synth_square(note, n, hz);
        if (bsp_audio_write(note, n * sizeof(int16_t)) != ESP_OK) return;
        done += n;
    }
    // 15ms 静音分音,让每个音"颗粒分明"
    memset(note, 0, CHIME_HZ * 15 / 1000 * sizeof(int16_t));
    bsp_audio_write(note, CHIME_HZ * 15 / 1000 * sizeof(int16_t));
}

static void chime_task(void *arg) {
    (void)arg;
    int16_t *note = malloc(NOTE_BUF_N * sizeof(int16_t));
    if (note == NULL) {
        ESP_LOGW(TAG, "note buf alloc failed, skip chime");
        vTaskDelete(NULL);
        return;
    }

    if (bsp_audio_init() != ESP_OK ||
        bsp_audio_set_format(CHIME_HZ, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "audio unavailable, skip chime");
        free(note);
        vTaskDelete(NULL);
        return;
    }
    bsp_audio_set_volume(70);

    // 经典上升琶音,尾音拉长收束
    static const struct { int hz, ms; } seq[] = {
        {659, 85}, {784, 85}, {988, 85}, {1319, 260},
    };
    for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); ++i) {
        play_note(note, seq[i].hz, seq[i].ms);
    }

    // 尾音余韵:让 DMA 把最后一块送完再拆,避免"咔"声
    vTaskDelay(pdMS_TO_TICKS(80));
    bsp_audio_deinit();
    free(note);
    ESP_LOGI(TAG, "boot chime done");
    vTaskDelete(NULL);
}

void boot_chime_start(void) {
    if (xTaskCreate(chime_task, "boot_chime", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "chime task create failed");
    }
}
