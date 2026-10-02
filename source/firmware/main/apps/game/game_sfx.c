// main/apps/game/game_sfx.c —— 见 game_sfx.h 的设计说明。
#include "apps/game/game_sfx.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "game_sfx";

#define SFX_RATE  16000  /* 与采音同格式:bsp_audio_set_format(16000,16,1) */
#define SFX_CHUNK 128    /* 每块 8ms;栈上 256B */
#define SFX_AMP   4200   /* 方波幅度(±),比语音提示音略柔,避免刺耳 */

/* 开局竞态:game_start() 先起音效任务,采音任务要再过 ~200ms 才会
   bsp_audio_set_format() 把输出打开(实测 14473 请求写、14667 才 open)。
   旧实现"一次写失败就永久静音" → 开局那声必失败 → 整局都没音效。
   改为有界重试:写失败先等 codec 就绪,超时(600ms)才判定不可用。 */
#define SFX_READY_WAIT_MS 600
#define SFX_READY_STEP_MS 20

typedef struct {
    uint16_t hz;
    uint16_t ms;
} sfx_seg_t;

/* 音色表:每个音最多 3 段,段间无缝衔接。hz==0 表示结束。 */
static const sfx_seg_t kTone[GS_COUNT][3] = {
    [GS_START] = { { 740, 45 },  { 0, 0 },    { 0, 0 } },
    [GS_SCORE] = { { 1046, 55 }, { 0, 0 },    { 0, 0 } },
    [GS_DEAD]  = { { 330, 80 },  { 196, 140 },{ 0, 0 } },
    [GS_BEST]  = { { 659, 70 },  { 880, 70 }, { 1175, 140 } },
};

static TaskHandle_t s_task;
static volatile int s_pending;   /* 0 = 无请求;只保留最后一次,不排队 */
static bool s_muted;             /* codec 确实不可用后置 true,不再重试/刷日志 */

/* 写一块 PCM,失败则有界等待 codec 就绪后重试。
   返回 false = 等满 SFX_READY_WAIT_MS 仍写不进去(codec 真的没开)。 */
static bool write_blocking(const int16_t *buf, size_t bytes) {
    for (int waited = 0; waited < SFX_READY_WAIT_MS; waited += SFX_READY_STEP_MS) {
        if (bsp_audio_write(buf, bytes) == ESP_OK) return true;
        vTaskDelay(pdMS_TO_TICKS(SFX_READY_STEP_MS));
    }
    return false;
}

/* 生成并阻塞写出一段方波。I2S 写会等 DMA 完成并让出 CPU,不会饿死 LVGL。 */
static void play_seg(uint16_t hz, uint16_t ms) {
    if (hz == 0 || ms == 0) return;
    int16_t buf[SFX_CHUNK];
    const int period = SFX_RATE / hz;   /* 每周期采样数,>1 保证 */
    const int half = period > 1 ? period / 2 : 1;
    int left = (int)SFX_RATE * ms / 1000;
    int phase = 0;
    while (left > 0) {
        const int n = left < SFX_CHUNK ? left : SFX_CHUNK;
        for (int i = 0; i < n; ++i) {
            buf[i] = (phase < half) ? (int16_t)SFX_AMP : (int16_t)-SFX_AMP;
            if (++phase >= period) phase = 0;
        }
        if (!write_blocking(buf, (size_t)n * sizeof(int16_t))) {
            if (!s_muted) {
                s_muted = true;  /* 等满 600ms 仍写不进去——只报一次,别刷屏 */
                ESP_LOGW(TAG, "codec 输出未就绪(等 %dms),本局关闭音效", SFX_READY_WAIT_MS);
            }
            return;
        }
        left -= n;
    }
}

static void sfx_task(void *arg) {
    (void)arg;
    for (;;) {
        /* 等待通知:不占队列 RAM,也不需要额外的信号量。 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const int id = s_pending;
        s_pending = 0;
        if (id <= GS_NONE || id >= GS_COUNT || s_muted) continue;
        for (int i = 0; i < 3; ++i) {
            const sfx_seg_t *seg = &kTone[id][i];
            if (seg->hz == 0) break;
            play_seg(seg->hz, seg->ms);
        }
    }
}

void game_sfx_start(void) {
    if (s_task) return;
    s_pending = 0;
    s_muted = false;
    /* 优先级 5(高于 LVGL 4):提示音只有几十毫秒,且 I2S 写阻塞时会让出 CPU,
       不会拖慢渲染;低于它会让音效明显滞后于画面。 */
    if (xTaskCreate(sfx_task, "game_sfx", 2048, NULL, 5, &s_task) != pdPASS) {
        ESP_LOGW(TAG, "音效任务创建失败,本局无音效");
        s_task = NULL;
    }
}

void game_sfx_stop(void) {
    if (!s_task) return;
    vTaskDelete(s_task);
    s_task = NULL;
    s_pending = 0;
}

void game_sfx_play(game_sfx_t id) {
    if (!s_task || id <= GS_NONE || id >= GS_COUNT) return;
    s_pending = (int)id;
    xTaskNotifyGive(s_task);
}
