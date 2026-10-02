#include "apps/app_base.h"
#include "stopwatch_core.h"
#include "launcher/app_registry.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "app_fonts.h"
#include "ui_pixel.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

namespace {
stopwatch_t s_watch{};  // Retained across app switches; reset on device restart.
lv_obj_t *s_screen;
lv_timer_t *s_timer;
lv_obj_t *s_time;
const lv_font_t *s_time_font;
lv_obj_t *s_status;
lv_obj_t *s_primary;
lv_obj_t *s_secondary;
lv_obj_t *s_laps[STOPWATCH_LAPS];
ui_pixel_battery_t s_battery{};
uint64_t s_battery_updated_us;

lv_obj_t *label(const char *text, const lv_font_t *font, uint32_t color,
                int x, int y, int w, int h) {
    lv_obj_t *o = lv_label_create(s_screen);
    lv_label_set_text(o, text);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_label_set_long_mode(o, LV_LABEL_LONG_DOT);
    return o;
}
void set_text(lv_obj_t *obj, const char *text) {
    if (std::strcmp(lv_label_get_text(obj), text) != 0) lv_label_set_text(obj, text);
}
void refresh() {
    if (!s_screen) return;
    const uint64_t now = esp_timer_get_time();
    const uint64_t elapsed = stopwatch_elapsed(&s_watch, now);
    char time[40];
    stopwatch_format(elapsed, time, sizeof(time));
    // Hours get a smaller, bounded font so the value never runs over the edges.
    const lv_font_t *font = elapsed < 3600000000ULL
                               ? &lv_font_montserrat_32 : &lv_font_montserrat_20;
    if (font != s_time_font) {
        lv_obj_set_style_text_font(s_time, font, 0);
        s_time_font = font;
    }
    set_text(s_time, time);
    set_text(s_status, s_watch.running ? "计时中" : (elapsed ? "已暂停" : "准备计时"));
    set_text(s_primary, s_watch.running ? "暂停" : (elapsed ? "继续" : "开始"));
    set_text(s_secondary, s_watch.running ? "上键记段 · 长按确定返回" : "下键清零 · 长按确定返回");
    if (s_watch.running) app_registry_keep_awake();
    for (uint32_t i = 0; i < STOPWATCH_LAPS; ++i) {
        char row[72];
        if (i < s_watch.lap_count) {
            stopwatch_format(s_watch.laps_us[i], time, sizeof(time));
            std::snprintf(row, sizeof(row), "#%02lu     %s",
                          static_cast<unsigned long>(s_watch.lap_count - i), time);
        } else {
            std::snprintf(row, sizeof(row), "--");
        }
        set_text(s_laps[i], row);
    }
    if (!s_battery_updated_us || now - s_battery_updated_us >= 5000000) {
        ui_pixel_battery_set(&s_battery, bsp_battery_soc());
        s_battery_updated_us = now;
    }
}
void tick(lv_timer_t *) { refresh(); }
void init() {}
void start() {
    if (!bsp_lvgl_lock(1000)) return;
    if (s_screen) { bsp_lvgl_unlock(); return; }
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    lv_obj_t *top = ui_pixel_top_bar(s_screen);
    lv_obj_t *name = ui_pixel_label(top, "秒表", &buddy_font_16, UI_TEXT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);
    s_battery = ui_pixel_battery_create(top, 206, 6, UI_TEXT, &lv_font_montserrat_14);
    s_status = label("", &buddy_font_16, UI_ACCENT, 16, 46, 208, 24);
    s_time = label("", &lv_font_montserrat_32, UI_TEXT, 16, 86, 208, 46);
    lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_CENTER, 0);
    label("最近三段 · 退出保留计时", &buddy_font_16, UI_TEXT_DIM, 16, 144, 208, 22);
    for (int i = 0; i < STOPWATCH_LAPS; ++i)
        s_laps[i] = label("", &lv_font_montserrat_14, UI_TEXT, 16, 178 + 23 * i, 208, 20);
    lv_obj_t *rule = lv_obj_create(s_screen);
    lv_obj_remove_style_all(rule);
    lv_obj_set_pos(rule, 16, 252); lv_obj_set_size(rule, 208, 1);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(rule, lv_color_hex(UI_BORDER), 0);
    lv_obj_t *key = label("确定", &buddy_font_16, UI_ON_ACCENT, 16, 264, 44, 25);
    lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(key, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(key, 2, 0);
    s_primary = label("", &buddy_font_16, UI_TEXT, 70, 268, 154, 20);
    s_secondary = label("", &buddy_font_16, UI_TEXT_DIM, 16, 296, 208, 20);
    s_time_font = &lv_font_montserrat_32;
    s_battery_updated_us = 0;
    refresh();
    lv_screen_load(s_screen);
    s_timer = lv_timer_create(tick, s_watch.running ? 50 : 1000, nullptr);
    ESP_LOGI("stopwatch", "started; running=%d", s_watch.running);
    bsp_lvgl_unlock();
}
void stop() {
    if (!bsp_lvgl_lock(-1)) return;
    if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }
    if (s_screen) { lv_obj_delete(s_screen); s_screen = nullptr; }
    s_battery = {};
    bsp_lvgl_unlock();
}
void on_key(uint8_t btn, app_btn_event_t event, uint16_t) {
    if (event != BTN_EVT_CLICK || !bsp_lvgl_lock(500)) return;
    if (s_screen) {
        if (btn == BSP_BTN_OK) stopwatch_toggle(&s_watch, esp_timer_get_time());
        else if (btn == BSP_BTN_UP) stopwatch_lap(&s_watch, esp_timer_get_time());
        else if (btn == BSP_BTN_DOWN) stopwatch_reset(&s_watch);
        refresh();
        if (s_timer) lv_timer_set_period(s_timer, s_watch.running ? 50 : 1000);
        ESP_LOGI("stopwatch", "running=%d elapsed_us=%llu laps=%lu",
                 s_watch.running, (unsigned long long)stopwatch_elapsed(&s_watch, esp_timer_get_time()),
                 (unsigned long)s_watch.lap_count);
    }
    bsp_lvgl_unlock();
}
} // namespace

extern const passport_app_t g_stopwatch_app = {
    .id = APP_ID_STOPWATCH,
    .name = "秒表",
    .en_name = "STOPWATCH",
    .desc = "计时与最近三段记录\n退出后继续计时",
    .tag = "TIMER",
    .theme_color = 0xDDB36C,
    .init = init,
    .start = start,
    .stop = stop,
    .on_key = on_key,
};