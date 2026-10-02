#pragma once
#include <stdbool.h>

// BSP emits PRESS, then RELEASE/CLICK or LONG/LONG_UP. Once a gesture wakes
// the display, consume its entire tail. Only a fresh PRESS rearms app input.
typedef struct { bool waiting_for_press; } wake_gate_t;
static inline bool wake_gate_consume(wake_gate_t *gate, bool is_press) {
    if (!gate->waiting_for_press) return false;
    if (is_press) { gate->waiting_for_press = false; return false; }
    return true;
}
static inline void wake_gate_arm(wake_gate_t *gate) {
    gate->waiting_for_press = true;
}
