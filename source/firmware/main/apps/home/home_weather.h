// 首页天气:Open-Meteo(免 API Key)当前温度+WMO 天气码,映射为中文短语。
// 坐标由上传网页在浏览器端完成地理编码后写入资料,设备端只发一次小请求。
// 上次成功结果缓存到 NVS,无网开机也能显示最近一次天气。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    int16_t temp_c;    /* 四舍五入后的摄氏度 */
    uint16_t code;     /* WMO weather code */
    uint8_t humidity;  /* 相对湿度 % */
    uint8_t wind_kmh;  /* 10 米风速 km/h(取整) */
    uint16_t wind_dir; /* 10 米风向,度(0=北,顺时针) */
} home_weather_t;

// 读 NVS 缓存。UI 立即有"上次天气"可显示。
void home_weather_init(void);

bool home_weather_get(home_weather_t *out);

// WMO 码 → 中文短语(全部在 buddy 字体字库内)。
const char *home_weather_code_text(uint16_t code);

// 蒲福风级:km/h → 0..12。
int home_weather_beaufort(uint8_t wind_kmh);

// 风向 → 中文方位("北风"/"东南风";静风返回"无风")。字形均在 buddy 字库内。
const char *home_weather_wind_dir_text(uint16_t deg);

// 阻塞拉取(lat/lon 为十进制字符串)。在首页后台任务里调用,勿持 LVGL 锁。
esp_err_t home_weather_refresh(const char *lat, const char *lon);

// 每次成功刷新递增,UI 据此刷新天气行。
uint32_t home_weather_version(void);

#ifdef __cplusplus
}
#endif
