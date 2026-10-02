// 俄罗斯方块(tc_core):纯逻辑层,不碰 LVGL。
// 内存:棋盘 200 字节静态 + 常量形状表在 flash,游戏过程零堆分配。
// C3 无 FPU,全部整数运算。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TC_COLS 10
#define TC_ROWS 20
#define TC_PIECE_KINDS 7

typedef enum {
    TC_READY = 0,
    TC_PLAYING,
    TC_DEAD,
} tc_phase_t;

// 复位到 READY(清板/清分)。
void tc_reset(void);

// READY → PLAYING。seed 驱动 7-bag 洗牌(喂 esp_timer)。
void tc_start(uint32_t seed);

// 周期性操作。任何变更后用 tc_cur_cells/tc_board 重绘。
void tc_move(int dx);   /* -1 左移 +1 右移 */
void tc_rotate(void);   /* 顺时针旋转,带踢墙 */
void tc_hard_drop(void);/* 直落到底并锁定 */
void tc_gravity(void);  /* 重力一拍:下落一格;着地则锁定→消行→出下一块 */

tc_phase_t tc_phase(void);
int tc_score(void);
int tc_lines(void);
int tc_level(void);

// 棋盘快照:ROWS*COLS 字节,0=空,1..7=方块颜色索引。 falling piece 不含在内。
const uint8_t *tc_board(void);

// 当前方块 4 格的绝对坐标(cx 0..9, cy 可能 -1..-2 表示还没进板);无块返回 false。
bool tc_cur_cells(int8_t out[4][2]);

// 当前方块颜色索引(1..7,与 tc_board 值同色系);无块返回 0。
int tc_cur_id(void);
int tc_next_id(void);  // 1..7, same palette index as tc_cur_id

// 下一块 4 格的相对形状(0..3 范围),用于预览。
void tc_next_cells(int8_t out[4][2]);

#ifdef __cplusplus
}
#endif
