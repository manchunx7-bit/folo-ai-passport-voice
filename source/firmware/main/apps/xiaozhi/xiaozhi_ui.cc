// Xiaozhi: shared terminal conversation view and bounded UTF-8 replies.
#include "apps/xiaozhi/xiaozhi_ui.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "bsp_audio.h"
#include "ui_pixel.h"
#include "utf8_tail.h"
#include "app_fonts.h"
#include "wifi_manager.h"
#include "lvgl.h"
#include <esp_log.h>
#include <cstdio>

namespace xiaozhi {

namespace {

// 状态点颜色:与整机状态色语义一致(墨/纸/草/黄/红/天蓝)
#define kTextMain (UI_TEXT)
#define kTextMuted (UI_SUB)
#define kUserBlue (UI_SKY_DARK)
#define kGreenAccent (UI_GRASS_DARK)
#define kAmberAccent (UI_ORANGE)
#define kRedAccent (UI_RED)

lv_obj_t *s_screen = nullptr;
lv_obj_t *s_status_badge = nullptr;
lv_obj_t *s_status_label = nullptr;
lv_obj_t *s_msg_container = nullptr;
lv_obj_t *s_user_text_label = nullptr;
lv_obj_t *s_ai_text_label = nullptr;
lv_obj_t *s_hint_label = nullptr;
lv_obj_t *s_volume_label = nullptr;

// 文本内容也只在持锁时读写，避免与 lv_label 生命周期脱节。
std::string s_user_text;
std::string s_ai_reply_buffer;

constexpr size_t kMaxTextBytes = 1200;  // up to 400 three-byte Han characters

// 滚动到消息区底部，保证最新一轮对话可见
void scroll_to_bottom_locked() {
    if (s_msg_container) {
        lv_obj_update_layout(s_msg_container);
        lv_obj_scroll_to_y(s_msg_container, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

// 所有"改文案"的操作都走这里：先加锁、再加一次指针校验。
// 返回 false 表示控件已销毁（应用正在退出），调用方静默丢弃即可。
bool begin_update(uint32_t timeout_ms) {
    if (!bsp_lvgl_lock(timeout_ms)) return false;
    if (s_screen == nullptr) {                      // 加锁期间被 ui_hide 销毁
        bsp_lvgl_unlock();
        return false;
    }
    return true;
}

} // namespace

void ui_init() {
    if (!bsp_lvgl_lock(1000)) return;

    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = nullptr;
    }
    s_status_badge = nullptr;
    s_status_label = nullptr;
    s_msg_container = nullptr;
    s_user_text_label = nullptr;
    s_ai_text_label = nullptr;
    s_hint_label = nullptr;
    s_volume_label = nullptr;
    s_user_text.clear();
    s_ai_reply_buffer.clear();

    s_screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_pixel_background(s_screen);

    lv_obj_t *bar = ui_pixel_top_bar(s_screen);
    lv_obj_t *title = ui_pixel_label(bar, "小智 AI", &buddy_font_16, UI_TEXT);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);
    auto battery = ui_pixel_battery_create(bar, 206, 6, UI_TEXT, &lv_font_montserrat_14);
    ui_pixel_battery_set(&battery, bsp_battery_soc());
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *speaker = lv_obj_create(bar);
        lv_obj_remove_style_all(speaker);
        lv_obj_set_pos(speaker, 95 + i * 4, i ? 7 : 10);
        lv_obj_set_size(speaker, 4, i ? 12 : 6);
        lv_obj_set_style_bg_opa(speaker, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(speaker, lv_color_hex(UI_TEXT_DIM), 0);
    }
    s_volume_label = ui_pixel_label(bar, "", &lv_font_montserrat_14, UI_TEXT_DIM);
    lv_obj_set_pos(s_volume_label, 110, 5);
    lv_label_set_text_fmt(s_volume_label, "%u%%", bsp_audio_get_master_volume());

    s_status_badge = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_status_badge);
    lv_obj_set_pos(s_status_badge, 12, 53);
    lv_obj_set_size(s_status_badge, 6, 6);
    lv_obj_set_style_bg_opa(s_status_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kAmberAccent), 0);
    s_status_label = ui_pixel_label(s_screen, "准备连接", &ui_font_20, UI_TEXT);
    lv_obj_set_pos(s_status_label, 28, 42);
    lv_obj_set_size(s_status_label, 200, 30);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_MODE_DOTS);

    // 2. 可滚动的消息区（长回复不再被裁掉）——纸面板+墨描边+硬阴影
    s_msg_container = lv_obj_create(s_screen);
    lv_obj_set_size(s_msg_container, 216, 184);
    lv_obj_set_pos(s_msg_container, 12, 80);
    lv_obj_set_style_bg_color(s_msg_container, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(s_msg_container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_msg_container, 0, 0);
    lv_obj_set_style_border_width(s_msg_container, 1, 0);
    lv_obj_set_style_border_side(s_msg_container, ui_theme_is_classic()
                                                      ? LV_BORDER_SIDE_FULL
                                                      : LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(s_msg_container, lv_color_hex(UI_BORDER), 0);
    lv_obj_set_style_pad_all(s_msg_container, 8, 0);
    lv_obj_set_style_pad_row(s_msg_container, 12, 0);
    lv_obj_set_style_text_line_space(s_msg_container, 4, 0);
    lv_obj_set_flex_flow(s_msg_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_msg_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(s_msg_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_msg_container, LV_SCROLLBAR_MODE_OFF);

    s_user_text_label = lv_label_create(s_msg_container);
    lv_obj_set_width(s_user_text_label, 196);
    lv_obj_set_style_text_font(s_user_text_label, &buddy_font_16, 0);
    lv_obj_set_style_text_color(s_user_text_label, lv_color_hex(kUserBlue), 0);
    lv_label_set_long_mode(s_user_text_label, LV_LABEL_LONG_WRAP);
    s_user_text = "连续语音对话";
    lv_label_set_text(s_user_text_label, s_user_text.c_str());

    s_ai_text_label = lv_label_create(s_msg_container);
    lv_obj_set_width(s_ai_text_label, 196);
    lv_obj_set_style_text_font(s_ai_text_label, &buddy_font_16, 0);
    lv_obj_set_style_text_color(s_ai_text_label, lv_color_hex(kTextMain), 0);
    lv_label_set_long_mode(s_ai_text_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_ai_text_label, "说完稍停，自动回复。");

    // 3. 底部按键指引(共享胶囊样式)
    s_hint_label = ui_pixel_footer(s_screen, "确定开始对话 · 上下音量", "长按确定返回", &buddy_font_16);

    bsp_lvgl_unlock();
}

void ui_show() {
    if (!bsp_lvgl_lock(1000)) return;
    if (s_screen == nullptr) {
        bsp_lvgl_unlock();
        ui_init();
        if (!bsp_lvgl_lock(1000)) return;
    }
    if (s_screen) lv_screen_load(s_screen);
    bsp_lvgl_unlock();
}

void ui_hide() {
    if (!bsp_lvgl_lock(1000)) return;
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = nullptr;
        s_status_badge = nullptr;
        s_status_label = nullptr;
        s_msg_container = nullptr;
        s_user_text_label = nullptr;
        s_ai_text_label = nullptr;
        s_hint_label = nullptr;
        s_volume_label = nullptr;
    }
    s_user_text.clear();
    s_ai_reply_buffer.clear();
    bsp_lvgl_unlock();
}

void ui_set_state(State state) {
    if (!begin_update(200)) return;

    switch (state) {
    case State::Disconnected:
        lv_label_set_text(s_hint_label, WifiManager::GetInstance().IsConnected() ? "确定重新连接 · 上下音量" : "请先用手机完成配网");
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kRedAccent), 0);
        if (!WifiManager::GetInstance().IsConnected()) {
            lv_label_set_text(s_status_label, "无线网络未连接");
        } else {
            lv_label_set_text(s_status_label, "小智未连接");
        }
        break;
    case State::Connecting:
        lv_label_set_text(s_hint_label, "连接中，请稍候");
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kAmberAccent), 0);
        lv_label_set_text(s_status_label, "正在连接小智");
        break;
    case State::Idle:
        lv_label_set_text(s_hint_label, "确定开始对话 · 上下音量");
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kGreenAccent), 0);
        lv_label_set_text(s_status_label, "准备就绪");
        break;
    case State::Listening:
        lv_label_set_text(s_hint_label, "确定结束对话 · 上下音量");
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kUserBlue), 0);
        lv_label_set_text(s_status_label, "正在聆听");
        break;
    case State::Thinking:
        lv_label_set_text(s_hint_label, "等待回复，请稍候");
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kAmberAccent), 0);
        lv_label_set_text(s_status_label, "正在思考");
        break;
    case State::Speaking:
        lv_label_set_text(s_hint_label, "确定打断回复 · 上下音量");
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kGreenAccent), 0);
        lv_label_set_text(s_status_label, "正在回复");
        break;
    case State::Error:
        lv_label_set_text(s_hint_label, "确定重新连接 · 上下音量");
        lv_obj_set_style_bg_color(s_status_badge, lv_color_hex(kRedAccent), 0);
        lv_label_set_text(s_status_label, "服务暂时不可用");
        break;
    }

    bsp_lvgl_unlock();
}

void ui_set_status_text(const std::string &text) {
    if (!begin_update(200)) return;
    lv_label_set_text(s_status_label, text.c_str());
    bsp_lvgl_unlock();
}

void ui_set_user_text(const std::string &text) {
    if (!begin_update(200)) return;
    s_user_text = "你：";
    passport::append_utf8_tail(s_user_text, text, kMaxTextBytes);
    lv_label_set_text(s_user_text_label, s_user_text.c_str());
    scroll_to_bottom_locked();
    bsp_lvgl_unlock();
}

void ui_set_ai_text(const std::string &text) {
    if (!begin_update(200)) return;
    const auto tail = passport::utf8_tail(text, kMaxTextBytes);
    s_ai_reply_buffer.assign(tail.data(), tail.size());
    std::string display = "小智：" + s_ai_reply_buffer;
    lv_label_set_text(s_ai_text_label, display.c_str());
    scroll_to_bottom_locked();
    bsp_lvgl_unlock();
}

void ui_append_ai_text(const std::string &text) {
    if (!begin_update(200)) return;

    passport::append_utf8_tail(s_ai_reply_buffer, text, kMaxTextBytes);

    std::string display = "小智：" + s_ai_reply_buffer;
    lv_label_set_text(s_ai_text_label, display.c_str());
    scroll_to_bottom_locked();
    bsp_lvgl_unlock();
}

void ui_set_hint(const std::string &text) {
    if (!begin_update(200)) return;
    lv_label_set_text(s_hint_label, text.c_str());
    bsp_lvgl_unlock();
}

void ui_set_volume(unsigned percent) {
    if (!begin_update(200)) return;
    lv_label_set_text_fmt(s_volume_label, "%u%%", percent);
    bsp_lvgl_unlock();
}

void ui_clear_text() {
    if (!begin_update(200)) return;
    s_ai_reply_buffer.clear();
    s_user_text = "正在收音...";
    lv_label_set_text(s_user_text_label, s_user_text.c_str());
    lv_label_set_text(s_ai_text_label, "");
    bsp_lvgl_unlock();
}

} // namespace xiaozhi
