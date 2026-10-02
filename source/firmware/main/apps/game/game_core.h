// 声控飞鸟(game_core):纯逻辑层,不碰 LVGL。
// C3 没有 FPU,全部用 Q4 定点数(1 单位 = 1/16 像素),乘除只用移位/小常数。
// 每帧(33ms)调一次 gc_step();帧长固定,物理参数按 30fps 整定。
//
// 2026-09-13 优化:不变动任何数值(玩法平衡 1:1 保留),只做
//   1. 把散落在 .c 里的手感/难度常数提到这里,集中成"调参区",改一处即可试手感;
//   2. 补上判定盒内缩等此前只写在注释里的魔数,让 UI 层能引用同一份定义;
//   3. 死因快照提供统一清零接口,避免重开时残留上一局的坐标。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GC_BIRD_X 56      /* 角色的固定横坐标(px) */
#define GC_BIRD_W 22
#define GC_BIRD_H 18
#define GC_TOP 26         /* 顶栏下沿:飞行区上界 */
#define GC_GROUND_Y 284   /* 草地上沿:飞行区下界 */
#define GC_PIPE_W 34
#define GC_PIPE_LIP 10    /* 管口加宽段高度 */
#define GC_PIPES 3        /* 管道池大小:循环复用,游戏中零分配 */

/* ---- 判定盒 ---- */

/* 碰撞盒相对精灵四边各内缩多少 px。3 = 视觉宽容(狗鼻子/耳朵探出 3px 不算撞)。
   想更硬核就调小(2 = 略难,0 = 像素级精确);想更宽容就调大。**改这里会改变
   难度**,回退:改回 3。 */
#define GC_HIT_INSET 3

/* 管口(lip)比管身左右各多出的 px。视觉上管口更宽,但碰撞只按管身算 ——
   这是有意的宽容处理,让玩家"擦着管口过去"不会莫名判死。 */
#define GC_PIPE_LIP_OVERHANG 4

/* ---- 难度曲线调参区 ----
   下面 6 个数就是全部难度来源,当前值与优化前完全一致。
   想调难度只动这里;每一项都标注了"调大/调小"的后果,方便回退。 */

/* 管道水平速度(Q4/帧)。16 Q4 = 1px/帧 ≈ 30px/s。
   调大 = 反应时间变短(更难);调小 = 更从容。当前 52 ≈ 98px/s。 */
#define GC_BASE_SPEED 52

/* 每得 1 分的速度增量与封顶分。当前 2/8 → 满分后 +16 Q4(≈112px/s)。
   调大 = 后期更紧张;封顶分调小 = 更早进入匀速期。 */
#define GC_SPEED_GAIN 2
#define GC_SPEED_BONUS_MAX 8

/* 管道水平间距(px)。当前 140 ≈ 1.3~1.4s 一根,留足调整时间。
   调小 = 更密集(更难),但小于 120 会挤压可达性约束。 */
#define GC_SPACING 140

/* 缺口高度:起始 120,每得 1 分收窄 2,最低 96。
   调小 GAP_MIN = 更硬核;当前 96 对 18px 高的狗已相当宽容。 */
#define GC_GAP_START 120
#define GC_GAP_MIN 96
#define GC_GAP_SHRINK 2

/* 相邻缺口垂直位移上限(px)。这是"可达性约束":超过狗的爬升/缓降能力就是必死,
   玩家会觉得"没撞任何东西就输了"(实测 12 分两连死才加的这条)。
   调大 = 更刺激但会出现不可达缺口;调小 = 更平缓。 */
#define GC_GAP_STEP_MAX 56

typedef enum {
    GC_READY = 0,  /* 待起跳:鸟悬浮呼吸,出声/按OK开始 */
    GC_PLAYING,
    GC_DEAD,
} gc_phase_t;

// 死因快照:reason 1=落地 2=撞缺口上方 3=撞缺口下方,坐标均为 px。
typedef struct {
    int reason;
    int32_t bird_y;
    int32_t pipe_x;
    int32_t gap_top;
    int32_t gap_h;
} gc_death_info_t;

typedef struct {
    int32_t x;      /* Q4 */
    int32_t gap_y;  /* 缺口顶,px(Q4) */
    int32_t gap_h;  /* 缺口高,px */
    bool scored;
} gc_pipe_t;

// 回到 READY;清空管道与得分。
void gc_reset(void);

// READY → PLAYING。seed 驱动缺口序列(用 esp_timer 喂,每局不同)。
void gc_start(uint32_t seed);

// 推进一帧(仅 PLAYING 有意义;READY/DEAD 直接返回)。
void gc_step(void);

// 声音档位 0..255:0 = 匀速缓降,255 = 最快匀速爬升,中间线性过渡。
// 速度被档位端点钳住,不会出现说话冲上天/安静瞬间坠地。
void gc_set_thrust(uint8_t thrust);

// 拍翅(OK 键):把本帧速度直接置为上冲值,先于声音推力生效。
void gc_flap(void);

gc_phase_t gc_phase(void);
int32_t gc_bird_y_q4(void);   /* 鸟顶边,Q4 */
int32_t gc_bird_vy_q4(void);  /* 垂直速度,Q4/帧 */
const gc_pipe_t *gc_pipe(int index);
int gc_score(void);
gc_death_info_t gc_death_info(void);  /* DEAD 态有效:死因+双方坐标,供日志定位 */

#ifdef __cplusplus
}
#endif
