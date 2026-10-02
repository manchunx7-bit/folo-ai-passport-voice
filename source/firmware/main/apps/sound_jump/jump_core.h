#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SJ_LEVELS 8
#define SJ_PLATFORMS 20
#define SJ_CRUMBLE_TICKS 160
#define SJ_TICK_MS 20
#define SJ_FLIGHT_TICKS 34
#define SJ_Q 256
typedef enum { SJ_SOLID, SJ_CRUMBLE, SJ_MOVING } sj_platform_kind_t;
typedef enum { SJ_STANDING, SJ_FLYING, SJ_DEAD, SJ_WON } sj_phase_t;
typedef enum { SJ_NO_FAILURE, SJ_CLIFF, SJ_SPIKE, SJ_BROKEN } sj_failure_t;
typedef struct { int16_t x,y,w; uint8_t kind,amplitude,spike; } sj_platform_t;
typedef struct { const char *name,*hint; uint8_t count,checkpoint_interval; sj_platform_t platforms[SJ_PLATFORMS]; } sj_level_t;
typedef struct {
 uint8_t level,standing,checkpoint,flight,power;
 uint32_t notes,checkpoint_notes,broken;
 uint16_t crumb_ticks[SJ_PLATFORMS],deaths;
 uint32_t ticks,elapsed_ticks,jumps;
 int32_t x,y,start_x,start_y;
 sj_phase_t phase; sj_failure_t failure;
 bool landed,perfect_landing;
} sj_game_t;
const sj_level_t *sj_level(unsigned level);
void sj_begin(sj_game_t *g,unsigned level);
bool sj_launch(sj_game_t *g,uint8_t power);
void sj_step(sj_game_t *g);
void sj_retry(sj_game_t *g);
int sj_platform_x(const sj_game_t *g,unsigned index);
int sj_distance(uint8_t power);
void sj_arc(const sj_game_t *g,unsigned tick,int *x,int *y);
// Runs the same future collision/motion code as a real jump. No aim assistance.
bool sj_predict(const sj_game_t *g,uint8_t power,int *x,int *y);
bool sj_is_checkpoint(const sj_game_t *g,unsigned index);
unsigned sj_note_count(uint32_t mask);
unsigned sj_note_total(const sj_game_t *g);
unsigned sj_stars(const sj_game_t *g);
#ifdef __cplusplus
}
#endif
