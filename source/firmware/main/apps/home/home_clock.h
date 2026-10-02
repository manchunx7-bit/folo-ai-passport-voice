// 首页时钟:Wi-Fi 就绪后用 SNTP 校系统时钟,UI 从系统时间 + 本地时区偏移
// 格式化日期。时区复用 time_sync(语音应用)的 NVS 设置,全系统一致。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 幂等启动 SNTP(需网络已就绪)。重复调用无害。
void home_clock_start(void);

// 停止 SNTP 并释放网络依赖(离开首页时调用)。已校准的系统时间继续走时。
void home_clock_stop(void);

// 系统时钟是否已校准(2023 年之后视为有效;PC 校时或 SNTP 均可达成)。
bool home_clock_valid(void);

// "12:34" / "--:--"
void home_clock_format_time(char *buf, size_t cap);

// "09月12日"(buddy 字体含数字与月日字形)
void home_clock_format_date(char *buf, size_t cap);

// "周六".."周日"
void home_clock_format_weekday(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
