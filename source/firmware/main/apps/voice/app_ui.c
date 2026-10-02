// One screen, one physical action. Status comes from device/PC telemetry.
#include "app_ui.h"
#include "app_fonts.h"
#include "audio_streamer.h"
#include "ui_pixel.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_screen, *s_link, *s_dot, *s_title, *s_mic;
static lv_obj_t *s_elapsed, *s_action, *s_hint, *s_notice, *s_bars[7];
static ui_pixel_battery_t s_battery;
static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    return o;
}
static lv_obj_t *text(const char *value, const lv_font_t *font, uint32_t color,
                       int x, int y, int w, int h) {
    lv_obj_t *o = ui_pixel_label(s_screen, value, font, color);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_label_set_long_mode(o, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    return o;
}
static void label(lv_obj_t *o, const char *value) {
    if (strcmp(lv_label_get_text(o), value)) lv_label_set_text(o, value);
}
static void hidden(lv_obj_t *o, bool hide) {
    if (hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}
esp_err_t app_ui_init(void) {
    if (s_screen) return ESP_OK;
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_pixel_background(s_screen);
    lv_obj_t *top = ui_pixel_top_bar(s_screen);
    s_dot = block(top, 12, 10, 6, 6, UI_TEXT_DIM);
    s_link = ui_pixel_label(top, "等待电脑", &buddy_font_16, UI_TEXT_DIM);
    lv_obj_align(s_link, LV_ALIGN_LEFT_MID, 24, 0);
    s_battery = ui_pixel_battery_create(top, 206, 6, UI_TEXT, &lv_font_montserrat_14);
    s_title = text("微信输入法", &ui_font_20, UI_TEXT, 16, 53, 208, 28);
    s_mic = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_mic);
    lv_obj_remove_flag(s_mic, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_mic, 88, 107);
    lv_obj_set_size(s_mic, 64, 64);
    block(s_mic, 22, 0, 20, 32, UI_ACCENT);
    for (int y = 7; y < 27; y += 7) block(s_mic, 26, y, 12, 2, UI_BG);
    block(s_mic, 14, 21, 3, 20, UI_TEXT_DIM);
    block(s_mic, 47, 21, 3, 20, UI_TEXT_DIM);
    block(s_mic, 14, 39, 36, 3, UI_TEXT_DIM);
    block(s_mic, 30, 42, 4, 12, UI_TEXT_DIM);
    block(s_mic, 22, 54, 20, 3, UI_TEXT_DIM);
    for (int i = 0; i < 7; i++) s_bars[i] = block(s_screen, 61 + i * 18, 153, 10, 2, UI_ACCENT);
    s_elapsed = text("00:00", &lv_font_montserrat_32, UI_TEXT, 16, 171, 208, 42);
    s_action = text("等待电脑连接", &buddy_font_16, UI_TEXT, 16, 215, 208, 24);
    s_notice = text("", &buddy_font_16, UI_DANGER, 16, 244, 208, 22);
    block(s_screen, 16, 276, 208, 1, UI_BORDER);
    s_hint = text("打开电脑端转发程序", &buddy_font_16, UI_SUB, 16, 281, 208, 20);
    text("长按确定返回", &buddy_font_16, UI_TEXT_DIM, 16, 301, 208, 19);
    app_ui_snapshot_t snap = {.state = APP_ST_READY, .screen_on = true};
    app_ui_render(&snap);
    return ESP_OK;
}
void app_ui_show(void) { if (s_screen) lv_screen_load(s_screen); }
void app_ui_hide(void) { /* Registry replaces the screen on navigation. */ }
void app_ui_deinit(void) {
    if (s_screen) lv_obj_delete(s_screen);
    s_screen = NULL;
    memset(s_bars, 0, sizeof(s_bars));
}
void app_ui_render(const app_ui_snapshot_t *snap) {
    if (!s_screen || !snap->screen_on) return;
    const bool ready = snap->link_up && snap->pc_ready;
    const bool recording = ready && snap->state == APP_ST_LISTENING;
    label(s_link, snap->link_up ? "电脑已连接" : "等待电脑");
    ui_pixel_bg_color(s_dot, ready ? UI_SUCCESS : UI_TEXT_DIM);
    ui_pixel_battery_set(&s_battery, snap->battery_available ? snap->battery_soc : -1);
    label(s_title, recording ? "正在传音" : "微信输入法");
    hidden(s_mic, recording);
    hidden(s_elapsed, !recording);
    const uint16_t peak = recording ? audio_streamer_peak() : 0;
    unsigned height = (unsigned)peak * 54 / 6000;
    if (height > 54) height = 54;
    static const unsigned weights[7] = {35, 60, 85, 100, 85, 60, 35};
    for (int i = 0; i < 7; i++) {
        hidden(s_bars[i], !recording);
        unsigned h = height * weights[i] / 100;
        if (h < 2) h = 2; // Flat baseline for silence, never synthetic movement.
        lv_obj_set_height(s_bars[i], h);
        lv_obj_set_y(s_bars[i], 155 - h);
    }
    char timer[20];
    unsigned sec = snap->elapsed_ms / 1000;
    snprintf(timer, sizeof(timer), "%02u:%02u", sec / 60, sec % 60);
    label(s_elapsed, timer);
    label(s_action, !snap->link_up ? "等待电脑连接" : !ready ? "麦克风通道未就绪" :
                   recording ? "松开上键结束" : "按住上键说话");
    label(s_hint, !snap->link_up ? "打开电脑端转发程序" : !ready ? "请在电脑端检查通道" :
                 recording ? "对着设备麦克风说话" : "松开结束");
    label(s_notice, snap->toast[0] ? snap->toast : snap->net_busy ? "网络不稳，请靠近路由器" : "");
}
