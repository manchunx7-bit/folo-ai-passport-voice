// main/apps/voice/mode.h —— 链路通道抽象
#pragma once

#include "app_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool mode_link_up(void);
bool mode_channel_up(uint8_t chan);
int mode_send_event_line(uint8_t chan, const char *line, size_t len);
int mode_send_event_line_important(uint8_t chan, const char *line, size_t len,
                                   uint32_t timeout_ms);
const char *mode_audio_format(uint8_t chan);
void mode_select_audio_sender(uint8_t chan);
bool mode_wired(void);

#ifdef __cplusplus
}
#endif
