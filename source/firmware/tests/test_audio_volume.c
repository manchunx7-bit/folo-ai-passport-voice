#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bsp_audio_volume.h"

static int device;
static esp_codec_dev_vol_map_t curve_points[8];
static int curve_count;
static char calls[8];
static int call_count;
static int level;
static bool muted;
static int fail_volume, fail_mute, fail_curve;

int esp_codec_dev_set_vol_curve(esp_codec_dev_handle_t dev, esp_codec_dev_vol_curve_t *curve) {
    assert(dev == &device);
    if (fail_curve) return fail_curve;
    assert(curve->count <= 8);
    curve_count = curve->count;
    memcpy(curve_points, curve->vol_map, sizeof(curve_points[0]) * curve_count);
    return 0;
}
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t dev, int volume) {
    assert(dev == &device);
    calls[call_count++] = 'V';
    if (fail_volume) return fail_volume;
    level = volume;
    return 0;
}
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t dev, bool mute) {
    assert(dev == &device);
    calls[call_count++] = mute ? 'M' : 'U';
    if (fail_mute) return fail_mute;
    muted = mute;
    return 0;
}
static float decibels(int volume) {
    for (int i = 1; i < curve_count; ++i) {
        if (volume <= curve_points[i].vol) {
            const esp_codec_dev_vol_map_t lo = curve_points[i - 1];
            const esp_codec_dev_vol_map_t hi = curve_points[i];
            return lo.db_value + (hi.db_value - lo.db_value) *
                (volume - lo.vol) / (hi.vol - lo.vol);
        }
    }
    return curve_points[curve_count - 1].db_value;
}
static void apply(uint8_t app, uint8_t master, int expected, bool mute, const char *order) {
    memset(calls, 0, sizeof(calls));
    call_count = 0;
    assert(bsp_audio_volume_apply(&device, app, master) == 0);
    assert(level == expected);
    assert(muted == mute);
    assert(strcmp(calls, order) == 0);
}
int main(void) {
    assert(bsp_audio_volume_configure(&device) == 0);
    assert(curve_count >= 2);
    assert(curve_points[0].vol == 0);
    assert(curve_points[curve_count - 1].vol == 100);
    for (int i = 1; i < curve_count; ++i) {
        assert(curve_points[i].vol > curve_points[i - 1].vol);
        assert(curve_points[i].db_value > curve_points[i - 1].db_value);
    }
    // The change must preserve existing low-volume listening levels, increase
    // the top end by no more than 6 dB, and never regress as the user turns up.
    for (int i = 0; i <= 100; ++i) {
        if (i <= 80) assert(fabsf(decibels(i) - (-50.0f + 0.5f * i)) < 0.001f);
        if (i > 0) assert(decibels(i) >= decibels(i - 1));
        assert(decibels(i) <= 6.0f);
    }
    assert(fabsf(decibels(100) - 6.0f) < 0.001f);
    for (int app = 0; app <= 100; ++app) {
        for (int master = 0; master <= 100; ++master) {
            int v = bsp_audio_volume_effective(app, master);
            assert(v <= app && v <= master);
            if (app == 0 || master == 0) assert(v == 0);
            if (master > 0) assert(v >= bsp_audio_volume_effective(app, master - 1));
        }
    }
    // Radio -> muted recording -> TTS, including format reopen and master mute.
    apply(55, 80, 44, false, "VU");
    apply(100, 100, 100, false, "VU");
    apply(0, 100, 0, true, "MV");
    apply(100, 90, 90, false, "VU");
    muted = false; level = 100;  // emulate codec reopening with reset state
    apply(0, 90, 0, true, "MV");
    apply(100, 90, 90, false, "VU");
    apply(100, 0, 0, true, "MV");
    apply(255, 255, 100, false, "VU");
    apply(0, 255, 0, true, "MV");
    // Failed level changes must not unmute the previous output level.
    fail_volume = -7; call_count = 0;
    assert(bsp_audio_volume_apply(&device, 100, 100) == -7);
    assert(muted && call_count == 1 && calls[0] == 'V');
    fail_volume = 0; fail_mute = -8; call_count = 0;
    assert(bsp_audio_volume_apply(&device, 0, 100) == -8);
    assert(call_count == 1 && calls[0] == 'M');
    fail_mute = 0; fail_curve = -9;
    assert(bsp_audio_volume_configure(&device) == -9);
    puts("Audio volume regression: PASS");
    return 0;
}
