#pragma once

#include <stdint.h>
#include "esp_codec_dev.h"

int bsp_audio_volume_configure(esp_codec_dev_handle_t dev);
uint8_t bsp_audio_volume_effective(uint8_t requested, uint8_t master);
int bsp_audio_volume_apply(esp_codec_dev_handle_t dev, uint8_t requested, uint8_t master);
