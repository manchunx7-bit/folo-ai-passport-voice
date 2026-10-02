#include "stopwatch_core.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

uint64_t stopwatch_elapsed(const stopwatch_t *s, uint64_t now_us) {
    /* Monotonic clock: no dependency on UI refreshes or wall-clock sync. */
    return s->accumulated_us +
           ((s->running && now_us >= s->started_us) ? now_us - s->started_us : 0);
}
void stopwatch_toggle(stopwatch_t *s, uint64_t now_us) {
    if (s->running) {
        s->accumulated_us = stopwatch_elapsed(s, now_us);
        s->running = false;
    } else {
        s->started_us = now_us;
        s->running = true;
    }
}
bool stopwatch_lap(stopwatch_t *s, uint64_t now_us) {
    if (!s->running || s->lap_count == UINT32_MAX) return false;
    const uint64_t total = stopwatch_elapsed(s, now_us);
    for (int i = STOPWATCH_LAPS - 1; i > 0; --i) s->laps_us[i] = s->laps_us[i - 1];
    s->laps_us[0] = total - s->last_lap_us;
    s->last_lap_us = total;
    ++s->lap_count;
    return true;
}
bool stopwatch_reset(stopwatch_t *s) {
    if (s->running) return false;
    memset(s, 0, sizeof(*s));
    return true;
}
void stopwatch_format(uint64_t elapsed_us, char *out, size_t capacity) {
    uint64_t cs = elapsed_us / 10000;
    uint64_t sec = cs / 100;
    if (sec < 3600) {
        snprintf(out, capacity, "%02" PRIu64 ":%02" PRIu64 ".%02" PRIu64,
                 sec / 60, sec % 60, cs % 100);
    } else {
        snprintf(out, capacity, "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%02" PRIu64,
                 sec / 3600, sec / 60 % 60, sec % 60, cs % 100);
    }
}