#pragma once
#include <stdbool.h>
typedef void *esp_codec_dev_handle_t;
typedef struct { int vol; float db_value; } esp_codec_dev_vol_map_t;
typedef struct { esp_codec_dev_vol_map_t *vol_map; int count; } esp_codec_dev_vol_curve_t;
int esp_codec_dev_set_vol_curve(esp_codec_dev_handle_t dev, esp_codec_dev_vol_curve_t *curve);
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t dev, int volume);
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t dev, bool mute);
