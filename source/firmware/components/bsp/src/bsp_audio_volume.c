#include "bsp_audio_volume.h"

int bsp_audio_volume_configure(esp_codec_dev_handle_t dev) {
    // Preserve the low-noise curve through 80%. Reserve the upper range for
    // extra speaker headroom; +6 dB is a tuning candidate, not a measured
    // distortion-free limit. Do not restore the previous +18 dB maximum.
    static const esp_codec_dev_vol_map_t points[] = {
        { .vol = 0,   .db_value = -50.0f },
        { .vol = 80,  .db_value = -10.0f },
        { .vol = 90,  .db_value = -2.0f },
        { .vol = 100, .db_value = 6.0f },
    };
    esp_codec_dev_vol_curve_t curve = {
        .vol_map = (esp_codec_dev_vol_map_t *)points,
        .count = sizeof(points) / sizeof(points[0]),
    };
    return esp_codec_dev_set_vol_curve(dev, &curve);
}

uint8_t bsp_audio_volume_effective(uint8_t requested, uint8_t master) {
    if (requested > 100) requested = 100;
    if (master > 100) master = 100;
    return (uint8_t)(((uint16_t)requested * master) / 100U);
}

int bsp_audio_volume_apply(esp_codec_dev_handle_t dev, uint8_t requested, uint8_t master) {
    const uint8_t volume = bsp_audio_volume_effective(requested, master);
    int result;
    // A curve value of -50 dB is attenuation, not mute. Mute before changing
    // the level, and only unmute a nonzero request after the new level is set.
    if (volume == 0) {
        result = esp_codec_dev_set_out_mute(dev, true);
        if (result != 0) return result;
    }
    result = esp_codec_dev_set_out_vol(dev, volume);
    if (result != 0) return result;
    return volume == 0 ? 0 : esp_codec_dev_set_out_mute(dev, false);
}
