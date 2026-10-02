#include "apps/game/tetris_core.h"
#include <assert.h>
#include <string.h>

static void check_piece(void) {
    int8_t cells[4][2];
    if (!tc_cur_cells(cells)) return;
    const uint8_t *board = tc_board();
    for (int i = 0; i < 4; ++i) {
        int x = cells[i][0], y = cells[i][1];
        assert(x >= 0 && x < TC_COLS && y < TC_ROWS);
        if (y >= 0) assert(!board[y * TC_COLS + x]);
        for (int j = 0; j < i; ++j)
            assert(x != cells[j][0] || y != cells[j][1]);
    }
}
int main(void) {
    for (unsigned seed = 1; seed <= 200; ++seed) {
        tc_start(seed);
        int original = tc_cur_id();
        assert(tc_next_id() >= 1 && tc_next_id() <= 7);
        for (int turn = 0; turn < 100 && tc_phase() == TC_PLAYING; ++turn) {
            int next = tc_next_id();
            for (int i = 0; i < 15; ++i) {
                tc_move((turn & 1) ? 1 : -1);
                tc_rotate();
                check_piece();
            }
            tc_hard_drop();
            if (tc_phase() == TC_PLAYING) assert(tc_cur_id() == next);
            const uint8_t *board = tc_board();
            for (int i = 0; i < TC_ROWS * TC_COLS; ++i) assert(board[i] <= 7);
        }
        tc_start(seed);
        assert(tc_cur_id() == original);
    }
    tc_reset();
    assert(tc_phase() == TC_READY && tc_score() == 0);
    uint8_t empty[TC_ROWS * TC_COLS] = {0};
    tc_move(1); tc_rotate(); tc_hard_drop(); tc_gravity();
    assert(memcmp(tc_board(), empty, sizeof(empty)) == 0);
    return 0;
}
