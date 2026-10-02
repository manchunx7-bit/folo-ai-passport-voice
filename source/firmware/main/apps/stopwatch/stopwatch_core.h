#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define STOPWATCH_LAPS 3
typedef struct {
    uint64_t accumulated_us;
    uint64_t started_us;
    uint64_t last_lap_us;
    uint64_t laps_us[STOPWATCH_LAPS]; /* newest first, each is a split duration */
    uint32_t lap_count;
    bool running;
} stopwatch_t;

uint64_t stopwatch_elapsed(const stopwatch_t *s, uint64_t now_us);
void stopwatch_toggle(stopwatch_t *s, uint64_t now_us);
bool stopwatch_lap(stopwatch_t *s, uint64_t now_us);
bool stopwatch_reset(stopwatch_t *s); /* ignored while running */
void stopwatch_format(uint64_t elapsed_us, char *out, size_t capacity);
#ifdef __cplusplus
}
#endif