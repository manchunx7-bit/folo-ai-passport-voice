// main/apps/home/band_art.h —— 内置底图(A8)。见 band_art.c 头注释。
#pragma once

#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BAND_ART_W 240
#define BAND_ART_H 80

extern const uint8_t kBandArtCity[BAND_ART_W * BAND_ART_H];
extern const uint8_t kBandArtMountain[BAND_ART_W * BAND_ART_H];

#ifdef __cplusplus
}
#endif
