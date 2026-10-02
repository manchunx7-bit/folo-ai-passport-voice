#include "apps/game/tetris_core.h"

#include <stddef.h>

#define TC_PIECES 7

// 7 种方块 × 4 旋转态 × 4 格 (ox,oy)。标准 SRS 形状。
static const int8_t kShapes[TC_PIECES][4][4][2] = {
    { /* I */
        {{0,1},{1,1},{2,1},{3,1}}, {{2,0},{2,1},{2,2},{2,3}},
        {{0,1},{1,1},{2,1},{3,1}}, {{2,0},{2,1},{2,2},{2,3}},
    },
    { /* O */
        {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}},
        {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}},
    },
    { /* T */
        {{0,1},{1,1},{2,1},{1,0}}, {{1,0},{1,1},{1,2},{2,1}},
        {{0,1},{1,1},{2,1},{1,2}}, {{1,0},{1,1},{1,2},{0,1}},
    },
    { /* S */
        {{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}},
        {{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}},
    },
    { /* Z */
        {{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}},
        {{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}},
    },
    { /* J */
        {{0,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{1,2}},
        {{0,1},{1,1},{2,1},{2,2}}, {{1,0},{1,1},{0,2},{1,2}},
    },
    { /* L */
        {{2,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{2,2}},
        {{0,1},{1,1},{2,1},{0,2}}, {{0,0},{1,0},{1,1},{1,2}},
    },
};

static const int8_t kKick[5] = {0, -1, 1, -2, 2};

static struct {
    tc_phase_t phase;
    uint8_t board[TC_ROWS][TC_COLS];
    int8_t cur;        /* 0..6 */
    int8_t next;
    int8_t rot, px, py;
    int score, lines, level;
    uint32_t rng;
    uint8_t bag[TC_PIECES];
    uint8_t bag_pos;
} s;

static uint32_t next_rand(void) {
    s.rng = s.rng * 1664525u + 1013904223u;
    return s.rng >> 8;
}

static uint8_t bag_next(void) {
    if (s.bag_pos >= TC_PIECES) { /* 7-bag:洗满一袋再逐个出 */
        for (int i = 0; i < TC_PIECES; ++i) s.bag[i] = (uint8_t)i;
        for (int i = TC_PIECES - 1; i > 0; --i) {
            uint8_t j = (uint8_t)(next_rand() % (uint32_t)(i + 1));
            uint8_t t = s.bag[i];
            s.bag[i] = s.bag[j];
            s.bag[j] = t;
        }
        s.bag_pos = 0;
    }
    return s.bag[s.bag_pos++];
}

static bool hit(int8_t pxc, int8_t pyc, int8_t rotp) {
    const int8_t (*cells)[2] = kShapes[s.cur][rotp];
    for (int i = 0; i < 4; ++i) {
        const int8_t cx = pxc + cells[i][0];
        const int8_t cy = pyc + cells[i][1];
        if (cx < 0 || cx >= TC_COLS || cy >= TC_ROWS) return true;
        if (cy >= 0 && s.board[cy][cx]) return true;
        /* cy<0:还没进板,视为可行 */
    }
    return false;
}

static void spawn(void) {
    s.cur = s.next;
    s.next = (int8_t)bag_next();
    s.rot = 0;
    s.px = 3;
    s.py = -1;
    /* 出生点就被埋住 → 顶死 */
    if (hit(s.px, s.py, s.rot) && !hit(s.px, 0, s.rot)) s.py = 0;
    if (hit(s.px, s.py, s.rot)) s.phase = TC_DEAD;
}

static void lock_and_spawn(void) {
    const int8_t (*cells)[2] = kShapes[s.cur][s.rot];
    bool topped = false;
    for (int i = 0; i < 4; ++i) {
        const int8_t cx = s.px + cells[i][0];
        const int8_t cy = s.py + cells[i][1];
        if (cy < 0) {
            topped = true; /* 有格子悬在板外锁定 = 堆到顶 */
            continue;
        }
        s.board[cy][cx] = (uint8_t)(s.cur + 1);
    }
    if (topped) {
        s.phase = TC_DEAD;
        return;
    }

    /* 消行 */
    static const uint16_t kLineScore[5] = {0, 100, 300, 500, 800};
    int cleared = 0;
    for (int row = TC_ROWS - 1; row >= 0; --row) {
        bool full = true;
        for (int c = 0; c < TC_COLS; ++c) {
            if (!s.board[row][c]) {
                full = false;
                break;
            }
        }
        if (!full) continue;
        ++cleared;
        for (int r = row; r > 0; --r) {
            for (int c = 0; c < TC_COLS; ++c) s.board[r][c] = s.board[r - 1][c];
        }
        for (int c = 0; c < TC_COLS; ++c) s.board[0][c] = 0;
        ++row; /* 同行重查(整体下移后) */
    }
    if (cleared) {
        s.lines += cleared;
        s.level = s.lines / 10 + 1;
        s.score += kLineScore[cleared > 4 ? 4 : cleared] * s.level;
    }
    spawn();
}

void tc_reset(void) {
    s.phase = TC_READY;
    for (int r = 0; r < TC_ROWS; ++r)
        for (int c = 0; c < TC_COLS; ++c) s.board[r][c] = 0;
    s.score = 0;
    s.lines = 0;
    s.level = 1;
    s.bag_pos = TC_PIECES; /* 强制重洗 */
}

void tc_start(uint32_t seed) {
    tc_reset();
    s.rng = seed ? seed : 0x9E3779B9u;
    s.next = (int8_t)bag_next();
    spawn();
    s.phase = TC_PLAYING;
}

void tc_move(int dx) {
    if (s.phase != TC_PLAYING) return;
    if (!hit((int8_t)(s.px + dx), s.py, s.rot)) s.px = (int8_t)(s.px + dx);
}

void tc_rotate(void) {
    if (s.phase != TC_PLAYING) return;
    const int8_t nr = (int8_t)((s.rot + 1) & 3);
    for (int i = 0; i < 5; ++i) {
        if (!hit((int8_t)(s.px + kKick[i]), s.py, nr)) {
            s.px = (int8_t)(s.px + kKick[i]);
            s.rot = nr;
            return;
        }
    }
}

void tc_hard_drop(void) {
    if (s.phase != TC_PLAYING) return;
    while (!hit(s.px, (int8_t)(s.py + 1), s.rot)) s.py = (int8_t)(s.py + 1);
    lock_and_spawn();
}

void tc_gravity(void) {
    if (s.phase != TC_PLAYING) return;
    if (!hit(s.px, (int8_t)(s.py + 1), s.rot)) {
        s.py = (int8_t)(s.py + 1);
        return;
    }
    lock_and_spawn();
}

tc_phase_t tc_phase(void) { return s.phase; }
int tc_score(void) { return s.score; }
int tc_lines(void) { return s.lines; }
int tc_level(void) { return s.level; }
const uint8_t *tc_board(void) { return &s.board[0][0]; }

bool tc_cur_cells(int8_t out[4][2]) {
    if (s.phase != TC_PLAYING) return false;
    const int8_t (*cells)[2] = kShapes[s.cur][s.rot];
    for (int i = 0; i < 4; ++i) {
        out[i][0] = (int8_t)(s.px + cells[i][0]);
        out[i][1] = (int8_t)(s.py + cells[i][1]);
    }
    return true;
}

int tc_next_id(void) { return s.next + 1; }

int tc_cur_id(void) { return s.phase == TC_PLAYING ? s.cur + 1 : 0; }

void tc_next_cells(int8_t out[4][2]) {
    const int8_t (*cells)[2] = kShapes[s.next][0];
    for (int i = 0; i < 4; ++i) {
        out[i][0] = cells[i][0];
        out[i][1] = cells[i][1];
    }
}
