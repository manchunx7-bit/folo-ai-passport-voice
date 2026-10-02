// Application drawer: shared phosphor palette and pixel icons.
#include "launcher/launcher.h"
#include "launcher/app_registry.h"
#include "wifi_manager.h"
#include "ssid_manager.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "app_fonts.h"
#include "esp_log.h"
#include "lvgl.h"

#include <cstdio>

static const char *TAG = "launcher";

namespace {

constexpr size_t kMaxApps = APP_ID_COUNT - 2; // excludes launcher and home
constexpr size_t kIconParts = 10;

// 旅行仪器配色:与首页(app_home.cc)完全同源,风格统一。
#define kBg (UI_BG)          // 屏底炭黑
#define kCardBg (UI_SURFACE)      // 卡片底
#define kPhos (UI_ACCENT)        // 琥珀:图标/标题/选中态
#define kPhosDim (UI_TEXT_DIM)     // 灰米色:副标题/未选中
#define kPhosBorder (UI_BORDER)  // 卡片/底板描边

lv_obj_t *s_screen;
lv_obj_t *s_card;
lv_obj_t *s_icon_plate;            // 图标底板(墨块+主题色描边)
lv_obj_t *s_chip;                  // 英文副标题徽章(主题色底)
lv_obj_t *s_app_title;
lv_obj_t *s_app_subtitle;
lv_obj_t *s_page_number;
lv_obj_t *s_icon;
lv_obj_t *s_icon_parts[kIconParts];
lv_obj_t *s_page_dots[kMaxApps];
lv_obj_t *s_wifi_bars[3];
ui_pixel_battery_t s_battery;
size_t s_dot_count;
size_t s_selected_index;

const char *app_chinese_name(passport_app_id_t id) {
    // 2026-09-13 修复(D1):名称原本在 launcher 里硬编码一份、在 passport_app_t
    // 里又各写一份,两边悄悄跑偏(注册表"城市收音机" vs 抽屉"收音机",
    // "随身 AI 语音" vs "AI语音")。改成**以注册表为唯一来源**,只有没注册进
    // 注册表的内置页(配网/设置)才走兜底表。
    const passport_app_t *app = app_registry_get_by_id(id);
    if (app != nullptr && app->name != nullptr && app->name[0] != '\0') {
        return app->name;
    }
    switch (id) {
        case APP_ID_WIFI: return "无线网络";
        case APP_ID_SETTINGS: return "设置";
        default: return "";
    }
}

// 像素块:方角(至多 1px 圆角),保证所有图标是同一套"积木"语言
void set_part(size_t index, int x, int y, int w, int h, uint32_t color) {
    if (index >= kIconParts) return;
    lv_obj_t *part = s_icon_parts[index];
    lv_obj_clear_flag(part, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(part, x, y);
    lv_obj_set_size(part, w, h);
    lv_obj_set_style_radius(part, 0, 0);
    lv_obj_set_style_border_width(part, 0, 0);
    lv_obj_set_style_bg_color(part, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
}

// 描边块:纸底 + 墨边,用于图标里的"空心"结构(机身/屏幕)
void set_frame(size_t index, int x, int y, int w, int h, uint32_t color,
               int border) {
    if (index >= kIconParts) return;
    lv_obj_t *part = s_icon_parts[index];
    lv_obj_clear_flag(part, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(part, x, y);
    lv_obj_set_size(part, w, h);
    lv_obj_set_style_radius(part, 0, 0);
    lv_obj_set_style_border_width(part, border, 0);
    lv_obj_set_style_border_color(part, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(part, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
}

// 88×88 画布上的像素图标,全部用 4px 网格对齐的方块拼
void draw_app_icon(passport_app_id_t id, uint32_t color) {
    for (lv_obj_t *part : s_icon_parts) lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);

    switch (id) {
        case APP_ID_HOME:
            // 小房子:阶梯屋顶 + 描边屋身 + 门 + 窗
            set_part(0, 14, 30, 60, 8, color);
            set_part(1, 22, 22, 44, 8, color);
            set_part(2, 32, 14, 24, 8, color);
            set_frame(3, 20, 38, 48, 34, color, 4);
            set_part(4, 40, 50, 10, 22, color);   // 门
            set_part(5, 26, 44, 8, 8, color);     // 窗
            break;
        case APP_ID_RADIO:
            // 收音机:机身 + 喇叭点阵 + 刻度窗 + 天线
            set_frame(0, 10, 26, 68, 46, color, 4);
            set_part(1, 18, 34, 24, 14, color);   // 喇叭
            set_part(2, 18, 54, 24, 4, color);
            set_part(3, 18, 62, 24, 4, color);
            set_part(4, 50, 34, 20, 8, color);    // 刻度窗
            set_part(5, 50, 48, 20, 4, color);
            set_part(6, 24, 14, 40, 4, color);    // 天线
            set_part(7, 60, 10, 8, 8, color);     // 天线头
            break;
        case APP_ID_VOICE:
            // 麦克风:拾音头 + 支架 + 底座
            set_frame(0, 30, 8, 28, 44, color, 4);
            set_part(1, 20, 34, 6, 16, color);    // 左弧
            set_part(2, 62, 34, 6, 16, color);    // 右弧
            set_part(3, 20, 50, 48, 6, color);    // 底弧
            set_part(4, 41, 56, 6, 12, color);    // 立杆
            set_part(5, 28, 68, 32, 6, color);    // 底座
            break;
        case APP_ID_XIAOZHI:
            // 机器人头(吉祥物同族):头 + 大眼 + 天线 + 围巾
            set_frame(0, 16, 18, 56, 44, color, 4);
            set_part(1, 28, 30, 8, 12, color);    // 左眼
            set_part(2, 52, 30, 8, 12, color);    // 右眼
            set_part(3, 38, 50, 12, 4, color);    // 嘴
            set_part(4, 42, 8, 4, 10, color);     // 天线
            set_part(5, 38, 4, 12, 6, color);      // 天线头
            set_part(6, 26, 64, 36, 6, color);     // 围巾
            break;
        case APP_ID_WIFI:
            // 信号塔:四级阶梯(与其它图标的"方块"语言一致)
            set_part(0, 14, 56, 12, 18, color);
            set_part(1, 32, 44, 12, 30, color);
            set_part(2, 50, 30, 12, 44, color);
            set_part(3, 68, 16, 12, 58, color);
            break;
        case APP_ID_SETTINGS:
            // 滑块三行:横轨 + 错位方块钮
            set_part(0, 14, 20, 60, 6, color);
            set_part(1, 14, 41, 60, 6, color);
            set_part(2, 14, 62, 60, 6, color);
            set_part(3, 26, 14, 16, 18, color);
            set_part(4, 52, 35, 16, 18, color);
            set_part(5, 22, 56, 16, 18, color);
            break;
        case APP_ID_GAME:
            // 你在狗叫什么: 像素狗头(柴犬大脸 + 双折耳 + 眼鼻 + 汪汪大嘴)
            set_frame(0, 16, 26, 52, 38, color, 4);   // 狗脸主体
            set_part(1, 10, 18, 14, 16, color);       // 左耳
            set_part(2, 60, 18, 14, 16, color);       // 右耳
            set_part(3, 26, 36, 8, 8, color);         // 左眼
            set_part(4, 50, 36, 8, 8, color);         // 右眼
            set_part(5, 38, 44, 8, 6, color);         // 黑鼻头
            set_part(6, 32, 54, 20, 8, color);        // 汪汪张开的大嘴
            break;
        case APP_ID_SOUND_JUMP:
            set_frame(0, 28, 12, 28, 26, color, 4);
            set_part(1, 34, 20, 4, 6, color);
            set_part(2, 46, 20, 4, 6, color);
            set_part(3, 10, 62, 24, 8, color);
            set_part(4, 58, 48, 24, 8, color);
            set_part(5, 38, 46, 4, 4, color);
            set_part(6, 48, 40, 4, 4, color);
            break;
        case APP_ID_STOPWATCH:
            set_part(0, 34, 6, 20, 6, color);
            set_part(1, 41, 12, 6, 8, color);
            set_part(2, 66, 18, 10, 6, color);
            set_frame(3, 16, 24, 56, 50, color, 4);
            set_part(4, 41, 34, 6, 20, color);
            set_part(5, 47, 48, 14, 6, color);
            break;
        case APP_ID_TETRIS:
            // 俄罗斯方块:T/L/方 三种积木错落
            set_frame(0, 12, 14, 30, 10, color, 3);   // T 横条
            set_part(1, 22, 24, 10, 10, color);       // T 竖
            set_part(2, 12, 44, 10, 10, color);       // L 竖
            set_part(3, 12, 54, 10, 10, color);
            set_part(4, 22, 54, 10, 10, color);
            set_part(5, 48, 24, 10, 10, color);       // O 方块
            set_part(6, 58, 24, 10, 10, color);
            set_part(7, 48, 34, 10, 10, color);
            set_part(8, 58, 34, 10, 10, color);
            set_part(9, 40, 58, 38, 10, color);       // I 长条
            break;
        default:
            break;
    }
}

void update_card_content() {
    const passport_app_t *app = app_registry_get_by_index(s_selected_index);
    if (!app) return;
    lv_label_set_text(s_app_title, app_chinese_name(app->id));
    lv_label_set_text(s_app_subtitle, app->desc ? app->desc : "");
    lv_label_set_text_fmt(s_page_number, "%u / %u", (unsigned)s_selected_index + 1,
                          (unsigned)app_registry_get_count());
    draw_app_icon(app->id, kPhos);   // 单色磷光图标,与首页同一视觉语言

    for (size_t i = 0; i < s_dot_count; ++i) {
        const bool selected = i == s_selected_index;
        lv_obj_set_size(s_page_dots[i], selected ? 18 : 8, 3);
        lv_obj_set_style_bg_color(s_page_dots[i],
                                  lv_color_hex(selected ? kPhos : kPhosDim), 0);
    }
}

// Selection follows physical UP/DOWN: only the icon settles, text stays readable.
// One 140ms animation, reset to the same resting position even on rapid repeats.
void icon_y_anim(void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, v); }

void card_nudge(int dir) {
    if (!s_icon) return;
    lv_anim_delete(s_icon, icon_y_anim);
    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, s_icon);
    lv_anim_set_exec_cb(&b, icon_y_anim);
    lv_anim_set_values(&b, 16 - 6 * dir, 16);
    lv_anim_set_duration(&b, 140);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_out);
    lv_anim_start(&b);
}

lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

}  // namespace

void launcher_ui_init(void) {
    bsp_lvgl_lock(-1);

    s_screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    // 与首页一致的绿磷光底(不再用天空/云/草地的像素风背景)
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(kBg), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    // ---- 状态栏 (y0..24):与首页同款 —— 炭灰条 + 共享 Wi-Fi 格 + 电池 ----
    ui_pixel_top_bar(s_screen);
    ui_pixel_wifi_bars_create(s_screen, 12, 18, s_wifi_bars);
    s_battery = ui_pixel_battery_create(s_screen, 206, 6, kPhos,
                                        &lv_font_montserrat_14);

    // ---- 应用卡:炭灰面板 + 细描边(与首页卡片同语言,无硬阴影) ----
    s_card = block(s_screen, 20, 44, 200, 208,
                   ui_theme_is_classic() ? kCardBg : kBg);
    lv_obj_set_style_border_width(s_card, 1, 0);
    lv_obj_set_style_border_side(s_card, ui_theme_is_classic()
                                             ? LV_BORDER_SIDE_FULL
                                             : LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(kPhosBorder), 0);

    lv_obj_t *catalogue = ui_pixel_label(s_screen, "应用", &buddy_font_16, UI_TEXT);
    lv_obj_set_pos(catalogue, 42, 3);
    s_page_number = ui_pixel_label(s_screen, "", &lv_font_montserrat_14, UI_TEXT_DIM);
    lv_obj_set_pos(s_page_number, 159, 29);
    lv_obj_set_width(s_page_number, 60);
    lv_obj_set_style_text_align(s_page_number, LV_TEXT_ALIGN_RIGHT, 0);

    // ---- 图标底板:近黑内凹框,磷光单色图标压在上面 ----
    s_icon_plate = block(s_card, 54, 14, 92, 92, kBg);
    lv_obj_set_style_border_width(s_icon_plate, ui_theme_is_classic() ? 1 : 0, 0);
    lv_obj_set_style_border_color(s_icon_plate, lv_color_hex(kPhosBorder), 0);

    s_icon = lv_obj_create(s_card);
    lv_obj_set_size(s_icon, 88, 88);
    lv_obj_align(s_icon, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_bg_opa(s_icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_icon, 0, 0);
    lv_obj_set_style_pad_all(s_icon, 0, 0);
    lv_obj_clear_flag(s_icon, LV_OBJ_FLAG_SCROLLABLE);
    for (lv_obj_t *&part : s_icon_parts) {
        part = lv_obj_create(s_icon);
        lv_obj_set_style_pad_all(part, 0, 0);
        lv_obj_clear_flag(part, LV_OBJ_FLAG_SCROLLABLE);
    }

    s_app_title = ui_pixel_label_ls(s_card, "", &ui_font_20, UI_TEXT, UI_LS_TITLE);
    lv_obj_set_size(s_app_title, 184, 28);
    lv_label_set_long_mode(s_app_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(s_app_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_app_title, LV_ALIGN_TOP_MID, 0, 116);

    // ---- 英文副标题:细框铭牌(灰米色描边 + 琥珀文字) ----
    s_chip = block(s_card, 8, 154, 182, 46, ui_theme_is_classic() ? kCardBg : kBg);
    lv_obj_set_style_border_width(s_chip, 0, 0);
    lv_obj_set_style_border_color(s_chip, lv_color_hex(kPhosBorder), 0);
    s_app_subtitle = lv_label_create(s_chip);
    lv_obj_set_style_text_font(s_app_subtitle, &buddy_font_16, 0);
    lv_obj_set_style_text_color(s_app_subtitle, lv_color_hex(UI_TEXT_DIM), 0);
    lv_obj_set_style_text_letter_space(s_app_subtitle, 0, 0);
    lv_obj_set_style_text_line_space(s_app_subtitle, 3, 0);
    lv_obj_set_size(s_app_subtitle, 182, 46);
    lv_label_set_long_mode(s_app_subtitle, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(s_app_subtitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_app_subtitle, LV_ALIGN_CENTER, 0, 0);

    // ---- 页点:像素小方块,选中拉长并染主题色 ----
    s_dot_count = app_registry_get_count();
    if (s_dot_count > kMaxApps) s_dot_count = kMaxApps;
    lv_obj_t *dots = lv_obj_create(s_screen);
    lv_obj_set_size(dots, 150, 12);
    lv_obj_align(dots, LV_ALIGN_TOP_MID, 0, 262);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 7, 0);
    lv_obj_set_style_pad_all(dots, 0, 0);
    lv_obj_set_style_bg_opa(dots, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dots, 0, 0);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_SCROLLABLE);
    for (size_t i = 0; i < s_dot_count; ++i) {
        s_page_dots[i] = lv_obj_create(dots);
        lv_obj_set_style_radius(s_page_dots[i], 0, 0);
        lv_obj_set_style_border_width(s_page_dots[i], 0, 0);
        lv_obj_set_style_pad_all(s_page_dots[i], 0, 0);
        lv_obj_clear_flag(s_page_dots[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    ui_pixel_footer(s_screen, "上下选择 · 确定打开", "长按确定返回首页", &buddy_font_16);

    update_card_content();
    launcher_ui_update_status();
    lv_scr_load(s_screen);
    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "Launcher UI initialized (travel instrument), %u apps", (unsigned)s_dot_count);
}

void launcher_ui_show(void) {
    if (!s_screen) {
        launcher_ui_init();
        return;
    }
    bsp_lvgl_lock(-1);
    update_card_content();
    launcher_ui_update_status();
    lv_scr_load(s_screen);
    bsp_lvgl_unlock();
}

void launcher_ui_hide(void) {
    // Launcher 不与重型 App 同时常驻。Radio 的音频驱动和复杂 UI 会把单核
    // C3 的内部堆推到极限，进入 App 前释放主页可避免 LVGL 分配失败崩溃。
    bsp_lvgl_lock(-1);
    if (s_screen) lv_obj_delete(s_screen);
    s_screen = nullptr;
    s_card = nullptr;
    s_icon_plate = nullptr;
    s_chip = nullptr;
    s_app_title = nullptr;
    s_app_subtitle = nullptr;
    s_page_number = nullptr;
    s_icon = nullptr;
    s_battery = {nullptr, nullptr};
    for (lv_obj_t *&part : s_icon_parts) part = nullptr;
    for (lv_obj_t *&dot : s_page_dots) dot = nullptr;
    for (lv_obj_t *&bar : s_wifi_bars) bar = nullptr;
    s_dot_count = 0;
    bsp_lvgl_unlock();
}

void launcher_ui_update_status(void) {
    if (!s_screen) return;
    ui_pixel_battery_set(&s_battery, bsp_battery_soc());

    const bool connected = WifiManager::GetInstance().IsConnected();
    int bars = 0;
    uint32_t color = kPhosDim;
    if (connected) {
        const int rssi = WifiManager::GetInstance().GetRssi();
        bars = rssi >= -60 ? 3 : (rssi >= -75 ? 2 : 1);
        color = kPhos;
    }
    ui_pixel_wifi_bars_set(s_wifi_bars, bars, color);
}

void launcher_ui_on_key(uint8_t btn, app_btn_event_t event) {
    if (event != BTN_EVT_CLICK && event != BTN_EVT_LONG) return;
    const size_t count = app_registry_get_count();
    if (!count) return;

    if (btn == BSP_BTN_UP && event == BTN_EVT_CLICK) {
        // Previous app: a short vertical icon cue, with text kept still.
        s_selected_index = (s_selected_index + count - 1) % count;
        bsp_lvgl_lock(-1);
        update_card_content();
        card_nudge(1);
        bsp_lvgl_unlock();
    } else if (btn == BSP_BTN_DOWN && event == BTN_EVT_CLICK) {
        // Next app: reverse the same short cue.
        s_selected_index = (s_selected_index + 1) % count;
        bsp_lvgl_lock(-1);
        update_card_content();
        card_nudge(-1);
        bsp_lvgl_unlock();
    } else if (btn == BSP_BTN_OK) {
        const passport_app_t *app = app_registry_get_by_index(s_selected_index);
        if (app) {
            ESP_LOGI(TAG, "User launched: %s (ID: %d)", app->name, app->id);
            app_registry_switch_to(app->id);
        }
    }
}
