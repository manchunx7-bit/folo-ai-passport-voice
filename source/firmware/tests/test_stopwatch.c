#include "apps/stopwatch/stopwatch_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    stopwatch_t s = {0};
    assert(stopwatch_elapsed(&s, 1234) == 0);
    assert(!stopwatch_lap(&s, 1000));
    stopwatch_toggle(&s, 1000000);
    assert(stopwatch_elapsed(&s, 2234567) == 1234567);
    assert(!stopwatch_reset(&s));
    assert(stopwatch_lap(&s, 2500000));
    assert(s.laps_us[0] == 1500000);
    stopwatch_toggle(&s, 3000000);
    assert(stopwatch_elapsed(&s, 9000000) == 2000000); /* paused time excluded */
    assert(!stopwatch_lap(&s, 9000000));
    stopwatch_toggle(&s, 10000000);
    assert(stopwatch_lap(&s, 11000000));
    assert(s.laps_us[0] == 1500000); /* spans pause/resume, excludes the pause */
    assert(stopwatch_elapsed(&s, 3610000000ULL) == 3602000000ULL); /* no UI ticks */
    assert(stopwatch_lap(&s, 3610000000ULL));
    assert(stopwatch_lap(&s, 3611000000ULL));
    assert(s.lap_count == 4 && s.laps_us[0] == 1000000);
    assert(s.laps_us[1] == 3599000000ULL && s.laps_us[2] == 1500000);
    stopwatch_toggle(&s, 3612000000ULL);
    assert(stopwatch_reset(&s));
    assert(!s.running && !s.lap_count && !stopwatch_elapsed(&s, UINT64_MAX));
    char out[40];
    stopwatch_format(59999999, out, sizeof(out)); assert(!strcmp(out, "00:59.99"));
    stopwatch_format(60000000, out, sizeof(out)); assert(!strcmp(out, "01:00.00"));
    stopwatch_format(3599999999ULL, out, sizeof(out)); assert(!strcmp(out, "59:59.99"));
    stopwatch_format(3600000000ULL, out, sizeof(out)); assert(!strcmp(out, "01:00:00.00"));
    stopwatch_format(360000000000ULL, out, sizeof(out)); assert(!strcmp(out, "100:00:00.00"));
    puts("Stopwatch monotonic timing, splits, pause/resume and reset: PASS");
    return 0;
}