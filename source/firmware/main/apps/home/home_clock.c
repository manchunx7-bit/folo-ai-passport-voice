#include "apps/home/home_clock.h"

#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"

#include <stdio.h>
#include <time.h>

#include "apps/voice/time_sync.h"

static const char *TAG = "home_clock";

#define CLOCK_EPOCH_MIN 1700000000LL /* 2023-11;早于此视为未校时 */

void home_clock_start(void) {
    if (esp_sntp_enabled()) return;
    // 单服务器(LWIP_SNTP_MAX_SERVERS=1),阿里云节点国内可达性最好;
    // 解析失败由 SNTP 自身按间隔重试,无需外层重试任务。
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started (ntp.aliyun.com)");
}

void home_clock_stop(void) {
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
        ESP_LOGI(TAG, "SNTP stopped");
    }
}

bool home_clock_valid(void) {
    time_t now = time(NULL);
    return now > CLOCK_EPOCH_MIN;
}

void home_clock_format_time(char *buf, size_t cap) {
    if (!buf || cap < 6) return;
    if (!home_clock_valid()) {
        snprintf(buf, cap, "--:--");
        return;
    }
    time_t t = time(NULL) + (time_t)time_sync_tz_hour() * 3600;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(buf, cap, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

void home_clock_format_date(char *buf, size_t cap) {
    if (!buf || cap < 16) return;
    if (!home_clock_valid()) {
        snprintf(buf, cap, "----");
        return;
    }
    time_t t = time(NULL) + (time_t)time_sync_tz_hour() * 3600;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(buf, cap, "%02d月%02d日", tm.tm_mon + 1, tm.tm_mday);
}

void home_clock_format_weekday(char *buf, size_t cap) {
    static const char *kWeek[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    if (!buf || cap < 8) return;
    if (!home_clock_valid()) {
        snprintf(buf, cap, "----");
        return;
    }
    time_t t = time(NULL) + (time_t)time_sync_tz_hour() * 3600;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(buf, cap, "%s", kWeek[tm.tm_wday % 7]);
}
