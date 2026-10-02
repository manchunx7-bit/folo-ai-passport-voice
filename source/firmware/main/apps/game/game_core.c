#include "apps/game/game_core.h"

#include <stddef.h>

// 物理常量(Q4/帧,30fps)。换算:16 Q4 = 1px/帧 = 30px/s。
// 垂直用"目标速度"模型:声音档位 0..255 线性映射 缓降↔爬升,vy 向目标平滑
// 收敛且被目标值钳住 —— 初版是重力/推力对抗加速,说话冲上天、安静瞬间坠地,
// 难度高到过不了第一关(用户实测)。
//
// 2026-09-13 优化:常量搬进 game_core.h 的"难度曲线调参区",本文件只保留
// 与难度无关的纯物理量(运动手感 + 天花板处理)。数值与优化前完全一致。
#define SINK4 37       /* 静音目标速度:匀速缓降 ≈69px/s,半屏约 2s,且保证
                          最快管道速度下也够得着向下 56px 的下一个缺口 */
#define CLIMB4 (-58)   /* 满声目标速度:匀速爬升 ≈109px/s,再大声也就这么快 */
#define FLAP4 (-50)    /* OK 拍翅:轻点一下 ≈ -94px/s */

/* vy 向目标速度收敛的分母:4 = 每帧收敛 1/4 ≈ 130ms 时间常数,跟手但不跳变。
   调小 = 更跟手但更"神经质";调大 = 更绵软。 */
#define VY_EASE_DIV 4

/* 天花板软处理:距顶 <CEIL_HARD 强制低头,CEIL_HARD~CEIL_SOFT 线性收油门。
   悬停带保持在最浅缺口(gap_y≥50)以下,否则鸟贴着天花板蹭,
   擦到浅缺口的管道边就是"看着没碰却死了"。 */
#define CEIL_HARD 25
#define CEIL_SOFT 50

/* 缺口随机带的上下留白(px):保证缺口不会贴着顶栏或草地生成。 */
#define BAND_MARGIN 24

static struct {
    gc_phase_t phase;
    int32_t y;       /* 鸟顶边,Q4 */
    int32_t vy;      /* Q4/帧 */
    int32_t thrust;  /* 0..255 声音档位 */
    bool flap_pending;
    int32_t last_gap_y; /* 上一根缺口顶(px):可达性约束的基准 */
    gc_death_info_t death; /* 死因快照,给上层日志用 */
    gc_pipe_t pipes[GC_PIPES];
    int score;
    uint32_t rng;
} s;

/* 当前缺口高度:随得分收窄,到 GC_GAP_MIN 封底。 */
static int32_t gap_h_now(void) {
    int32_t gap = GC_GAP_START - s.score * GC_GAP_SHRINK;
    return gap < GC_GAP_MIN ? GC_GAP_MIN : gap;
}

/* 当前管道速度:随得分略提速,到 GC_SPEED_BONUS_MAX 分封顶。 */
static int32_t speed_now(void) {
    const int32_t bonus =
        s.score > GC_SPEED_BONUS_MAX ? GC_SPEED_BONUS_MAX : s.score;
    return GC_BASE_SPEED + bonus * GC_SPEED_GAIN;
}

static uint32_t next_rand(void) {
    /* LCG:确定性、无 libc 依赖 */
    s.rng = s.rng * 1664525u + 1013904223u;
    return s.rng >> 8;
}

static void recycle_pipe(gc_pipe_t *p, int32_t x_q4) {
    const int32_t gap = gap_h_now();
    const int32_t band_top = GC_TOP + BAND_MARGIN;
    const int32_t span = (GC_GROUND_Y - GC_TOP) - gap - (BAND_MARGIN * 2);
    int32_t cy = band_top + (int32_t)(next_rand() % (uint32_t)span);
    /* 可达性约束:缺口顶相对上一根最多移动 GC_GAP_STEP_MAX,且夹回合法区间。
       否则随机落差超过狗的爬升/缓降能力,必撞,"什么都没碰就输了"。 */
    int32_t lo = s.last_gap_y - GC_GAP_STEP_MAX;
    int32_t hi = s.last_gap_y + GC_GAP_STEP_MAX;
    if (lo < band_top) lo = band_top;
    if (hi > band_top + span) hi = band_top + span;
    if (cy < lo) cy = lo;
    if (cy > hi) cy = hi;
    s.last_gap_y = cy;
    p->x = x_q4;
    p->gap_h = gap;
    p->gap_y = cy << 4;
    p->scored = false;
}

void gc_reset(void) {
    s.phase = GC_READY;
    s.y = ((GC_TOP + GC_GROUND_Y) / 2 - GC_BIRD_H / 2) << 4;
    s.vy = 0;
    s.thrust = 0;
    s.flap_pending = false;
    s.last_gap_y = GC_TOP + 60; /* 缺口链基准:合法带中部,首根管道就近出发 */
    /* 死因快照整体清零 —— 原来只清 reason,重开后 bird_y/pipe_x 还留着上一局
       的坐标,一旦新一局首帧就死会打出误导性的日志。 */
    s.death.reason = 0;
    s.death.bird_y = 0;
    s.death.pipe_x = 0;
    s.death.gap_top = 0;
    s.death.gap_h = 0;
    s.score = 0;
    for (int i = 0; i < GC_PIPES; ++i) {
        gc_pipe_t *p = &s.pipes[i];
        p->x = (240 + 60 + i * GC_SPACING) << 4;
        p->gap_h = GC_GAP_START;
        p->gap_y = (GC_TOP + 40) << 4;
        p->scored = true; /* READY 态不画不判 */
    }
}

void gc_start(uint32_t seed) {
    gc_reset();
    s.rng = seed ? seed : 0x1234u;
    for (int i = 0; i < GC_PIPES; ++i) {
        recycle_pipe(&s.pipes[i], (240 + 60 + i * GC_SPACING) << 4);
    }
    s.phase = GC_PLAYING;
}

void gc_set_thrust(uint8_t thrust) { s.thrust = thrust; }

void gc_flap(void) {
    if (s.phase == GC_PLAYING) s.flap_pending = true;
}

static bool hit(int32_t ax, int32_t ay, int32_t aw, int32_t ah,
                int32_t bx, int32_t by, int32_t bw, int32_t bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

void gc_step(void) {
    if (s.phase != GC_PLAYING) return;

    /* 垂直运动:声音档位线性映射 缓降↔爬升 的目标速度,vy 每帧向目标收敛
       1/4(~130ms 时间常数,跟手但不跳变)。速度上限就是目标端点,
       说话再响也只是匀速爬升,安静时也只是匀速缓降。 */
    int32_t target;
    if (s.flap_pending) {
        s.flap_pending = false;
        target = FLAP4; /* 拍翅:一次性轻点,随后仍向声音目标收敛 */
    } else {
        target = SINK4 + (int32_t)s.thrust * (CLIMB4 - SINK4) / 255;
        /* 贴顶处理:距顶 CEIL_HARD 内强制低头,CEIL_HARD~CEIL_SOFT 爬升随接近
           顶线性收油门。 */
        int32_t headroom = (s.y >> 4) - GC_TOP;
        if (headroom < 0) headroom = 0;
        if (headroom < CEIL_HARD) {
            target = SINK4 + 15;
        } else if (headroom < CEIL_SOFT && target < SINK4) {
            target = SINK4 + (target - SINK4) * (headroom - CEIL_HARD) /
                                 (CEIL_SOFT - CEIL_HARD);
        }
    }
    s.vy += (target - s.vy) / VY_EASE_DIV;
    s.y += s.vy;

    /* 管道推进 + 复用。speed_now() 本就是 Q4/帧,不能再 <<4 ——
       多移 4 位曾让管道以 1860px/s 闪现,开局一秒内必死。 */
    const int32_t speed = speed_now();
    int32_t rightmost = 0;
    for (int i = 0; i < GC_PIPES; ++i) {
        s.pipes[i].x -= speed;
        if (s.pipes[i].x > rightmost) rightmost = s.pipes[i].x;
    }
    for (int i = 0; i < GC_PIPES; ++i) {
        gc_pipe_t *p = &s.pipes[i];
        if (p->x < -(GC_PIPE_W << 4)) {
            recycle_pipe(p, rightmost + (GC_SPACING << 4));
        } else if (!p->scored && (p->x >> 4) + GC_PIPE_W < GC_BIRD_X) {
            p->scored = true;
            ++s.score;
        }
    }

    /* 碰撞:鸟盒内缩 GC_HIT_INSET px(视觉宽容)。落地即死;撞顶只贴顶滑行不判
       死 —— 否则"喊得越响死得越快",与声音越大上升越猛的直觉相反。 */
    const int32_t bx = GC_BIRD_X + GC_HIT_INSET;
    const int32_t by = (s.y >> 4) + GC_HIT_INSET;
    const int32_t bw = GC_BIRD_W - (GC_HIT_INSET * 2);
    const int32_t bh = GC_BIRD_H - (GC_HIT_INSET * 2);
    if (by < GC_TOP) {
        s.y = (int32_t)(GC_TOP - 2) << 4;
        if (s.vy < 0) s.vy = 0;
    }
    if (by + bh > GC_GROUND_Y) {
        s.death.reason = 1; /* ground */
        s.death.bird_y = by;
        s.phase = GC_DEAD;
        return;
    }
    for (int i = 0; i < GC_PIPES; ++i) {
        const gc_pipe_t *p = &s.pipes[i];
        const int32_t px = p->x >> 4;
        const int32_t gap_top = p->gap_y >> 4;
        /* 上半截高度必须用 gap_top-GC_TOP:直接拿 gap_y 当高度会让碰撞矩形
           比可见管道长出 GC_TOP=26px 隐形区,鸟没碰到柱子也判死(实测) */
        const bool hit_top =
            hit(bx, by, bw, bh, px, GC_TOP, GC_PIPE_W, gap_top - GC_TOP);
        const bool hit_bot =
            hit(bx, by, bw, bh, px, gap_top + p->gap_h, GC_PIPE_W,
                GC_GROUND_Y - gap_top - p->gap_h);
        if (hit_top || hit_bot) {
            s.death.reason = hit_top ? 2 : 3;
            s.death.bird_y = by;
            s.death.pipe_x = px;
            s.death.gap_top = gap_top;
            s.death.gap_h = p->gap_h;
            s.phase = GC_DEAD;
            return;
        }
    }
}

gc_phase_t gc_phase(void) { return s.phase; }
int32_t gc_bird_y_q4(void) { return s.y; }
int32_t gc_bird_vy_q4(void) { return s.vy; }
const gc_pipe_t *gc_pipe(int index) {
    return (index >= 0 && index < GC_PIPES) ? &s.pipes[index] : NULL;
}
int gc_score(void) { return s.score; }
gc_death_info_t gc_death_info(void) { return s.death; }
