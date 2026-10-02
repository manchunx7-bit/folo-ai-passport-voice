#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// One frame is 256 mono samples at 16 kHz. Only scalar levels are retained.
typedef enum { JV_NOISE, JV_READY } jv_phase_t;
#define JV_MIN_FRAMES 6
#define JV_FULL_FRAMES 75
#define JV_RELEASE_FRAMES 8
typedef struct {
 jv_phase_t phase;
 uint16_t noise,level,samples[100];
 uint16_t frames,voiced,quiet;
 uint8_t power,event_power,sensitivity;
 uint32_t sequence;
 bool armed,charging;
} jv_t;
uint16_t jv_rms(const int16_t *pcm,size_t n);
void jv_init(jv_t *v);
uint8_t jv_duration_power(unsigned frames);
void jv_frame(jv_t *v,uint16_t rms,bool listening);
#ifdef __cplusplus
}
#endif
