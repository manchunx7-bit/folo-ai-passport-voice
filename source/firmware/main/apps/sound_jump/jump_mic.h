#pragma once
#include "jump_voice.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {jv_phase_t phase;uint16_t level,noise;uint8_t progress,power,event_power,sensitivity;uint32_t sequence,updated_ms,calibration;bool charging,ready,error;} sj_mic_snapshot_t;
bool sj_mic_start(void);
void sj_mic_stop(void);
void sj_mic_listen(bool enabled);
void sj_mic_recalibrate(void);
void sj_mic_sensitivity(unsigned value);
sj_mic_snapshot_t sj_mic_snapshot(void);
#ifdef __cplusplus
}
#endif
