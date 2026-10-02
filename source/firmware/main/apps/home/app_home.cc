// 首页(个人主页)应用。
//
// 小内存约束下的三条硬规矩(改动时请保持):
//   1. 头像缓冲常驻 .bss(8KB),不做堆分配 —— 避免 8KB 连续块在堆碎片化后申请失败;
//   2. 文件 IO(读头像)与网络 IO(拉天气)一律在**不持 LVGL 锁**时完成;
//   3. stop() 必须删屏,与 Launcher 的 launcher_ui_hide() 同理,C3 内部堆没有余量
//      同时容纳两个应用的界面。
//
// 2026-09-13 视觉精致化(全部为"对象拼画 + 低频动画",无 canvas/图片资源):
//   - 昼夜主题:任务里每小时核对,19 点~次日 6 点切夜景(ui_pixel 主题系统);
//   - 天气卡左侧画像素天气图标(晴/少云/阴/雾/雨/雪/雷),拉取中显示四点 spinner;
//   - 无头像空态换全机吉祥物(会眨眼的像素小电视),替代灰色剪影;
//   - 底部白字加墨色错位硬阴影,压在草地上也清晰。
//
// 2026-09-13 晚 · 首页版式重构(上中下三层,用户定稿):
//   顶部:像素块大时钟(5×7 字模拼块,真·像素粗体,直接画在天空上)+ 日期行;
//   中部:组合卡 = 左头像(橙框) + 右侧 天气图标/短语 + 城市 + 温度 + 湿度·风级一行
//        (原两张统计卡降维合并进这一行);
//   底部:名牌卡 = 姓名(20px 标题字) + 个性签名,浮在天空上,草地只留按键提示。
//
// 2026-09-13 深夜 · 绿磷光锁屏改版(参照用户提供的复古终端风参考图):
//   整页黑底 + 磷光绿单色,三列布局:
//   左卡(104×202):人像区(96×140,头像经亮度阈值转 A8 单色) + 姓名 + 签名;
//   右列(120 宽):点阵时钟卡(5×7 字模,每格画 4×4 光点) / 日期+城市+Wi-Fi 卡 /
//                天气卡(单色图标 + 温度 + 短语 + 分隔线 + 风向·风级);
//   底部:240×108 A8 艺术底图全宽出血(tools/gen_band_art.py 生成,内置两张,
//        长按上键切换),电池悬浮在底图右上,按键提示压在底图下缘黑条上。
//   美术显示走 LVGL A8 快速路径(alpha 蒙版 + image_recolor 纯色),已核对
//   lv_draw_sw_img.c:233 与 lv_draw_sw_blend_to_rgb565.c。
#include "apps/home/app_home.h"

#include "apps/home/band_art.h"
#include "apps/home/home_clock.h"
#include "apps/home/home_profile.h"
#include "apps/home/home_upload.h"
#include "apps/home/home_weather.h"
#include "apps/voice/time_sync.h"
#include "launcher/app_registry.h"
#include "bsp_display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "src/libs/tjpgd/tjpgd.h"
#include "fonts/app_fonts.h"   // buddy_font_16:首页中文动态文本全靠它
#include "ui_pixel.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

extern "C" {
#include "bsp_battery.h"
}

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr char kTag[] = "app_home";

// 绿磷光配色(整页黑绿单色,参照复古终端锁屏参考图)。
#define kBg (UI_BG)          // 屏底近黑绿
#define kCardBg (UI_SURFACE)      // 卡片底(比屏底略亮)
#define kPhos (UI_TEXT)        // 磷光绿:主文字/点阵/艺术图
#define kPhosDim (UI_TEXT_DIM)     // 暗绿:次级文字
#define kPhosBorder (UI_BORDER)  // 卡片描边

// 兼容既有更新函数的语义色:磷光风格里"强调/在线"都是亮绿,弱化都是暗绿。
#define kText (kPhos)
#define kSub (kPhosDim)
#define kMuted (kPhosDim)
#define kAccent (kPhos)
#define kOnline (kPhos)
#define kOffline (kPhosDim)

// 版式栅格:所有元素都挂在这几个常量上,改一个边距整屏跟着走,不再散落硬编码。
constexpr int kMargin = 12;
constexpr int kContentW = 240 - kMargin * 2;  // 216

constexpr int64_t kWeatherFirstDelayMs = 15000;
constexpr int64_t kWeatherPeriodMs = 1800000;
constexpr int64_t kUploadTimeoutMs = 600000;

// 常驻 .bss:96×120 竖幅人像,1bpp 打包(浏览器端已完成亮度阈值+打包,仅 1440B)。
// 静态分配而非堆:首页与其他应用互斥运行,堆在切换瞬间最碎。
uint8_t s_portrait_1bpp[HOME_AVATAR_BYTES];
// 显示用 A8 展开:LVGL 软渲染 A8 快速路径(alpha 蒙版 + recolor 磷光绿纯色)。
// 2026-09-13 改为堆分配(home_start 申请 / home_stop 归还):14.4KB 常驻 .bss
// 会直接从堆里扣掉,而小智等应用恰恰在这块内存上跑 Opus 编码器 —— 之前
// largest 只剩 22KB,编码器(需约 25KB 连续块)必然 open 失败。首页与其它
// 应用互斥运行,切走时归还,谁在用谁付钱。
uint8_t *s_portrait_a8 = nullptr;
lv_image_dsc_t s_portrait_dsc;

std::atomic<bool> s_home_active{false};
// 界面存活开关:home_task 每轮持锁刷新前检查,stop() 删屏前置 false。
// 解决"stop 超时后任务仍活着却去写已删除 label"这一类的 use-after-free。
std::atomic<bool> s_ui_alive{false};
std::atomic<bool> s_force_weather{false};
TaskHandle_t s_task_handle = nullptr;
uint8_t s_detail_mode = 0;

lv_obj_t *s_screen = nullptr;
lv_obj_t *s_avatar_img = nullptr;      // 人像(A8 单色化后的头像)
lv_obj_t *s_portrait_ph = nullptr;     // 无头像占位文字
lv_obj_t *s_lbl_date = nullptr;
lv_obj_t *s_wifi_bars[3] = {nullptr, nullptr, nullptr};
ui_pixel_battery_t s_batt = {nullptr, nullptr};
lv_obj_t *s_band_img = nullptr;        // 底部艺术底图
bool s_share_visible = false;

// ---- 像素点阵时钟:4 个数字槽 + 冒号槽(槽 2),5×7 字模,每格一个 4×4 光点 ----
lv_obj_t *s_clk_slots[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
lv_obj_t *s_clk_colon[2] = {nullptr, nullptr};   // 冒号两点,每秒呼吸
char s_clk_slot_char[5] = {0, 0, 0, 0, 0};       // 各槽当前字符,变化才重绘
bool s_clk_colon_on = false;

lv_obj_t *s_lbl_name = nullptr;
lv_obj_t *s_lbl_sig = nullptr;
lv_obj_t *s_wicon_zone = nullptr;    // 天气图标绘制区(天气卡内 22×18)
lv_obj_t *s_lbl_city = nullptr;
lv_obj_t *s_lbl_wtext = nullptr;
lv_obj_t *s_lbl_temp = nullptr;
lv_obj_t *s_lbl_humidity = nullptr;  // 相对湿度(天气卡右下)
lv_obj_t *s_lbl_wind = nullptr;      // 风向·风级行
lv_obj_t *s_lbl_footer = nullptr;

// 底部艺术底图:0=城市夜景 1=山湖月色(均内置 flash);将来可扩展用户上传槽。
uint8_t s_band_index = 0;
lv_image_dsc_t s_band_dsc[2];

// ---- 天气像素图标(画在 s_wicon_zone 内,全部是色块) ----
constexpr int kWiMax = 12;
lv_obj_t *s_wicon_objs[kWiMax] = {};
int s_wicon_n = 0;
lv_obj_t *s_spin[4] = {nullptr, nullptr, nullptr, nullptr};  // 四点 spinner
uint8_t s_spin_phase = 0;
bool s_wicon_spinning = false;
bool s_weather_fetching = false;

// 1bpp 打包 → A8 展开(每字节 8 像素,MSB 在前,与网页端打包顺序一致)。
void expand_portrait_to_a8(void) {
    if (s_portrait_a8 == nullptr) return;   // 堆分配失败:走无头像占位
    for (int y = 0; y < HOME_PORTRAIT_H; ++y) {
        for (int x = 0; x < HOME_PORTRAIT_W; ++x) {
            const int bit =
                (s_portrait_1bpp[y * (HOME_PORTRAIT_W / 8) + x / 8] >> (7 - (x & 7))) & 1;
            s_portrait_a8[y * HOME_PORTRAIT_W + x] = bit ? 255 : 0;
        }
    }
}

// 无头像时显示占位文字(磷光风格不用彩色吉祥物)。
void apply_avatar(bool have_image) {
    if (have_image && s_portrait_a8 != nullptr) {
        expand_portrait_to_a8();
        lv_image_set_src(s_avatar_img, &s_portrait_dsc);
        lv_obj_clear_flag(s_avatar_img, LV_OBJ_FLAG_HIDDEN);
        if (s_portrait_ph) lv_obj_add_flag(s_portrait_ph, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_image_set_src(s_avatar_img, nullptr);
        lv_obj_add_flag(s_avatar_img, LV_OBJ_FLAG_HIDDEN);
        if (s_portrait_ph) lv_obj_clear_flag(s_portrait_ph, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---- 天气像素图标:WMO 码 → 方块拼画,与整机"积木"图标语言同源 ----
void update_weather_labels(const home_profile_t *prof);  // 定义在本段之后

lv_obj_t *px_container(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

lv_obj_t *px_block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    return o;
}

lv_obj_t *wicon_block(int x, int y, int w, int h, uint32_t color) {
    if (s_wicon_n >= kWiMax || !s_wicon_zone) return nullptr;
    // 磷光单色:原配色里的"亮色/墨"映射为亮绿,灰蓝系映射为暗绿(单色图标仍有层次)
    color = (color == UI_INK || color == UI_YELLOW || color == UI_ORANGE ||
             color == 0xFFFFFF)
                ? kPhos
                : kPhosDim;
    lv_obj_t *o = px_block(s_wicon_zone, x, y, w, h, color);
    s_wicon_objs[s_wicon_n++] = o;
    return o;
}

void wicon_clear(void) {
    if (!s_ui_alive.load() || s_screen == nullptr) {
        s_wicon_n = 0;
        for (lv_obj_t *&b : s_spin) b = nullptr;
        s_wicon_spinning = false;
        return;
    }
    for (int i = 0; i < s_wicon_n; ++i) {
        if (s_wicon_objs[i]) lv_obj_delete(s_wicon_objs[i]);
        s_wicon_objs[i] = nullptr;
    }
    s_wicon_n = 0;
    for (lv_obj_t *&b : s_spin) b = nullptr;
    s_wicon_spinning = false;
}

// 三段云 + 墨影,可复用在阴/雨/雪/雷/雾图标里
void wicon_cloud(int x, int y, uint32_t body) {
    wicon_block(x, y + 6, 18, 5, body);
    wicon_block(x + 3, y + 3, 9, 5, body);
    wicon_block(x + 9, y, 8, 5, body);
    wicon_block(x + 1, y + 11, 16, 2, UI_INK);
}

void wicon_sun(void) {
    wicon_block(7, 4, 8, 8, UI_YELLOW);   // 日芯
    wicon_block(10, 0, 3, 3, UI_ORANGE);  // 四向射线
    wicon_block(10, 13, 3, 3, UI_ORANGE);
    wicon_block(2, 7, 3, 3, UI_ORANGE);
    wicon_block(17, 7, 3, 3, UI_ORANGE);
    wicon_block(3, 1, 2, 2, UI_YELLOW);   // 斜向光点
    wicon_block(18, 1, 2, 2, UI_YELLOW);
    wicon_block(3, 13, 2, 2, UI_YELLOW);
    wicon_block(18, 13, 2, 2, UI_YELLOW);
}

void wicon_sun_cloud(void) {
    wicon_block(13, 0, 7, 7, UI_YELLOW);  // 右上太阳(下半被云压住)
    wicon_block(20, 2, 2, 2, UI_YELLOW);
    wicon_cloud(0, 4, 0xFFFFFF);
}

void weather_icon_draw(uint16_t code) {
    wicon_clear();
    if (code == 0) { wicon_sun(); return; }
    if (code == 1 || code == 2) { wicon_sun_cloud(); return; }
    if (code == 3) { wicon_cloud(2, 1, 0xB9C4CC); return; }              // 阴
    if (code == 45 || code == 48) {                                       // 雾
        wicon_cloud(2, 0, 0xC6CED4);
        wicon_block(4, 14, 14, 2, UI_SUB);
        return;
    }
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {       // 雨
        wicon_cloud(2, 0, 0x9AA7B4);
        wicon_block(4, 14, 2, 3, UI_SKY_DARK);
        wicon_block(9, 14, 2, 3, UI_SKY_DARK);
        wicon_block(14, 14, 2, 3, UI_SKY_DARK);
        return;
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {         // 雪
        wicon_cloud(2, 0, 0xD7DEE4);
        wicon_block(4, 14, 2, 2, 0x8FC7EF);
        wicon_block(9, 14, 2, 2, 0x8FC7EF);
        wicon_block(14, 14, 2, 2, 0x8FC7EF);
        return;
    }
    if (code >= 95) {                                                     // 雷
        wicon_cloud(2, 0, 0x8B98A5);
        wicon_block(9, 13, 3, 2, UI_YELLOW);
        wicon_block(8, 15, 2, 2, UI_YELLOW);
        return;
    }
    wicon_cloud(2, 1, 0xB9C4CC);
}

// 拉取中的四点 spinner:四个色块轮流点亮,由首页 1s tick 推进
void weather_spinner_draw(void) {
    wicon_clear();
    s_spin[0] = wicon_block(9, 1, 4, 3, UI_INK);
    s_spin[1] = wicon_block(15, 7, 3, 4, 0xB9C2B0);
    s_spin[2] = wicon_block(9, 13, 4, 3, 0xB9C2B0);
    s_spin[3] = wicon_block(4, 7, 3, 4, 0xB9C2B0);
    s_spin_phase = 0;
    s_wicon_spinning = true;
}

void weather_icon_spin_tick(void) {
    if (!s_wicon_spinning || !s_ui_alive.load() || s_screen == nullptr) return;
    s_spin_phase = (s_spin_phase + 1) & 3;
    for (int i = 0; i < 4; ++i) {
        if (s_spin[i]) {
            lv_obj_set_style_bg_color(
                s_spin[i], lv_color_hex(i == s_spin_phase ? kPhos : kPhosDim), 0);
        }
    }
}

// 天气区整体刷新:文字行 + 左侧图标。调用方需已持 LVGL 锁。
void refresh_weather_ui(void) {
    if (!s_ui_alive.load() || s_screen == nullptr) return;
    home_profile_t prof;
    home_profile_get(&prof);
    update_weather_labels(&prof);
    if (prof.lat[0] == '\0') {
        wicon_clear();                       // 未设城市:只留文字提示
    } else if (s_weather_fetching) {
        weather_spinner_draw();
    } else {
        home_weather_t w;
        if (home_weather_get(&w) && w.valid) {
            weather_icon_draw(w.code);
        } else {
            wicon_clear();                   // 无数据:不画,文字会提示"获取中"
        }
    }
}

// ---- 点阵时钟 ----
// 5×7 字模(行位掩码,bit4=最左列),每个点亮格画一个 4×4 光点(格距 5),
// 呈现 LED 点阵质感,与磷光锁屏参考图一致。不依赖任何字体文件。
constexpr int kCell = 5;             // 格距 → 数字 25×35
constexpr int kDot = 4;              // 光点边长(留 1px 缝出点阵感)
constexpr int kDigitW = 5 * kCell;   // 25
constexpr int kClockH = 7 * kCell;   // 35
constexpr uint8_t kDashGlyphIdx = 10;
const uint8_t kDigitGlyphs[11][7] = {
    {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F},  // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
    {0x1F, 0x01, 0x01, 0x1F, 0x10, 0x10, 0x1F},  // 2
    {0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F},  // 3
    {0x11, 0x11, 0x11, 0x1F, 0x01, 0x01, 0x01},  // 4
    {0x1F, 0x10, 0x10, 0x1F, 0x01, 0x01, 0x1F},  // 5
    {0x1F, 0x10, 0x10, 0x1F, 0x11, 0x11, 0x1F},  // 6
    {0x1F, 0x01, 0x02, 0x04, 0x04, 0x04, 0x04},  // 7
    {0x1F, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x1F},  // 8
    {0x1F, 0x11, 0x11, 0x1F, 0x01, 0x01, 0x1F},  // 9
    {0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00},  // '-' 占位
};

// 在槽位容器里按字模重绘一个字符(清空重画;时钟每分钟最多变一位,开销可忽略)
void draw_digit(lv_obj_t *slot, char c) {
    lv_obj_clean(slot);
    int idx = kDashGlyphIdx;
    if (c >= '0' && c <= '9') idx = c - '0';
    for (int r = 0; r < 7; ++r) {
        for (int col = 0; col < 5; ++col) {
            if (kDigitGlyphs[idx][r] & (0x10 >> col)) {
                px_block(slot, col * kCell, r * kCell, kDot, kDot, kPhos);
            }
        }
    }
}

// 数字槽重绘 + 日期 + 冒号每秒呼吸(时钟区唯一的逐秒动作,只动 opa 不重排)
void update_clock_labels() {
    char hh[3] = "--";
    char mm[3] = "--";
    char date_buf[24];
    const bool valid = home_clock_valid();
    if (valid) {
        char time_buf[8];
        home_clock_format_time(time_buf, sizeof(time_buf));
        hh[0] = time_buf[0];
        hh[1] = time_buf[1];
        mm[0] = time_buf[3];
        mm[1] = time_buf[4];
        char ymd[16];
        char week[8];
        home_clock_format_date(ymd, sizeof(ymd));
        home_clock_format_weekday(week, sizeof(week));
        std::snprintf(date_buf, sizeof(date_buf), "%s %s", ymd, week);
    } else {
        // 未校时一律显示占位符。时间只能来自 SNTP 或 PC 校时,设备自己没有可信来源。
        std::snprintf(date_buf, sizeof(date_buf), "等待校时");
    }

    const char digits[4] = {hh[0], hh[1], mm[0], mm[1]};
    const int slot_map[4] = {0, 1, 3, 4};   // 槽 2 是冒号
    for (int i = 0; i < 4; ++i) {
        const int slot = slot_map[i];
        if (s_clk_slots[slot] && s_clk_slot_char[slot] != digits[i]) {
            draw_digit(s_clk_slots[slot], digits[i]);
            s_clk_slot_char[slot] = digits[i];
        }
    }
    ui_pixel_label_set_text(s_lbl_date, date_buf);

    s_clk_colon_on = !s_clk_colon_on;
    for (lv_obj_t *dot : s_clk_colon) {
        if (dot) {
            lv_obj_set_style_opa(dot, s_clk_colon_on ? LV_OPA_COVER : LV_OPA_30, 0);
        }
    }
}

// 电量画成一个 24×12 的迷你电池:外框 + 按 SOC 伸缩的内条 + 右侧百分比数字。
// 比单纯的 "78%" 多占了两个对象,但一眼能读出剩余量,是值得的。
// 顶栏电池与 Wi-Fi 信号都用共享像素构件,与 Launcher/AI语音 顶栏一致。
void update_battery_label() {
    ui_pixel_battery_set(&s_batt, bsp_battery_soc());

    const bool connected = WifiManager::GetInstance().IsConnected();
    int bars = 0;
    uint32_t color = kOffline;
    if (connected) {
        const int rssi = WifiManager::GetInstance().GetRssi();
        bars = rssi >= -60 ? 3 : (rssi >= -75 ? 2 : 1);
        color = kOnline;
    }
    ui_pixel_wifi_bars_set(s_wifi_bars, bars, color);
}

// 天气拆三段:城市(弱) / 天气短语(次) / 温度(强调色 + 20px 数字)。
// 温度用 montserrat 而不是中文字库,是因为 "24°" 这种字形的数字比中文点阵细腻得多。
void update_weather_labels(const home_profile_t *prof) {
    if (!s_ui_alive.load() || s_lbl_city == nullptr) return;
    if (prof->lat[0] == '\0') {
        lv_label_set_text(s_lbl_city, "未设置城市");
        lv_label_set_text(s_lbl_wtext, "");
        lv_label_set_text(s_lbl_temp, "--");
        lv_label_set_text(s_lbl_humidity, "--");
        return;
    }
    home_weather_t w;
    const bool ok = home_weather_get(&w);
    lv_label_set_text(s_lbl_city, prof->city);
    if (ok && w.valid) {
        lv_label_set_text(s_lbl_wtext, home_weather_code_text(w.code));
        char t[12];
        std::snprintf(t, sizeof(t), "%d°", static_cast<int>(w.temp_c));
        lv_label_set_text(s_lbl_temp, t);
        // 只显示数字(不带"湿度"前缀):槽位 60px,65% 仅 24px,绝无折行
        char h[12];
        std::snprintf(h, sizeof(h), "湿%u%%", (unsigned)w.humidity);
        lv_label_set_text(s_lbl_humidity, h);
    } else {
        lv_label_set_text(s_lbl_wtext, s_weather_fetching ? "更新" : "暂无");
        lv_label_set_text(s_lbl_temp, "--");
        lv_label_set_text(s_lbl_humidity, "--");
    }
}

// 天气卡风级行:风向 + 蒲福风级(参考图同款"东南风 3级")。
void update_wind_line() {
    if (!s_ui_alive.load() || s_lbl_wind == nullptr) return;
    home_weather_t w;
    if (home_weather_get(&w) && w.valid) {
        char line[40];
        std::snprintf(line, sizeof(line), "%s %d级",
                      home_weather_wind_dir_text(w.wind_dir),
                      home_weather_beaufort(w.wind_kmh));
        ui_pixel_label_set_text(s_lbl_wind, line);
    } else {
        ui_pixel_label_set_text(s_lbl_wind, "风向 --");
    }
}

// 底部一行在"操作提示 / 上传提示 / 详情"之间轮换(下键切换),
// 上传模式优先级最高 —— 那时用户正盯着手机扫码,必须立刻看到 URL。
// 白字 + 墨色错位硬阴影(像素字投影),草地上的可读性更好。
void footer_set(const char *text, const lv_font_t *font, uint32_t color) {
    if (lv_obj_get_style_text_font(s_lbl_footer, LV_PART_MAIN) != font)
        lv_obj_set_style_text_font(s_lbl_footer, font, 0);
    ui_pixel_text_color(s_lbl_footer, color);
    ui_pixel_label_set_text(s_lbl_footer, text);

}

void update_footer_label() {
    if (home_upload_active()) {
        footer_set(home_upload_url(), &lv_font_montserrat_14, kPhos);
        return;
    }

    if (s_detail_mode == 1) {
        footer_set("长按上换图 · 长按下设置", &buddy_font_16, kPhos);
        return;
    }
    if (s_detail_mode == 2) {
        if (!WifiManager::GetInstance().IsConnected()) {
            footer_set("未连接无线网络", &buddy_font_16, kPhos);
            return;
        }
        home_weather_t w;
        const bool ok = home_weather_get(&w);
        const std::string ip = WifiManager::GetInstance().GetIpAddress();
        char line[48];
        std::snprintf(line, sizeof(line), "%s · %s", ip.c_str(),
                      ok && w.valid ? "最近天气" : "暂无天气");
        footer_set(line, &buddy_font_16, kPhos);
        return;
    }
    footer_set("上名片  下详情  确定应用", &buddy_font_16, kPhos);
}

void apply_profile(const home_profile_t *prof) {
    lv_label_set_text(s_lbl_name, prof->name[0] ? prof->name : "未设置姓名");
    lv_label_set_text(s_lbl_sig, prof->signature[0] ? prof->signature : "未设置签名");
    refresh_weather_ui();
}

lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                     lv_text_align_t align) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

// 卡片底板:磷光风 = 近黑绿底 + 1px 暗绿描边,无硬阴影(参考图卡片是细线框)。
lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                     bool bordered) {
    (void)radius;
    (void)bordered;
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, lv_color_hex(ui_theme_is_classic() ? kCardBg : kBg), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_side(p, ui_theme_is_classic()
                                        ? LV_BORDER_SIDE_FULL
                                        : LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(kPhosBorder), 0);
    lv_obj_set_style_pad_all(p, 2, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

void create_ui() {
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(kBg), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);

    // ---- 状态栏 (y0..24):Wi-Fi 信号 + 电池,回到最顶部 ----
    px_block(s_screen, 0, 0, 240, 24, kCardBg);
    px_block(s_screen, 0, 24, 240, 1, kPhosBorder);
    ui_pixel_wifi_bars_create(s_screen, 12, 18, s_wifi_bars);
    s_batt = ui_pixel_battery_create(s_screen, 206, 6, kPhos, &lv_font_montserrat_14);

    lv_obj_t *passport = ui_pixel_label(s_screen, "PASSPORT", &lv_font_montserrat_14, UI_TEXT_DIM);
    lv_obj_set_pos(passport, 42, 4);

    // ---- 左卡 (x6 y28 104×208):人像 + 姓名 + 签名 ----
    lv_obj_t *portrait_card = make_panel(s_screen, 6, 28, 104, 208, 0, true);
    // 人像区 96×150 内凹框:有头像显示单色化照片,无头像显示占位文字
    lv_obj_t *zone = px_container(portrait_card, 1, 1, 96, 150);
    lv_obj_set_style_border_width(zone, 1, 0);
    lv_obj_set_style_border_color(zone, lv_color_hex(kPhosBorder), 0);
    lv_obj_set_style_bg_color(zone, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(zone, LV_OPA_COVER, 0);

    s_avatar_img = lv_image_create(zone);
    lv_obj_set_pos(s_avatar_img, 0, 0);
    lv_obj_set_size(s_avatar_img, HOME_PORTRAIT_W, HOME_PORTRAIT_H);
    lv_image_set_inner_align(s_avatar_img, LV_IMAGE_ALIGN_CENTER);
    // A8 alpha 图以 recolor 色实心填充(LVGL 软渲染快速路径,已核对 lv_draw_sw_img.c)
    lv_obj_set_style_image_recolor(s_avatar_img, lv_color_hex(kPhos), 0);

    s_portrait_ph = make_label(zone, &buddy_font_16, kPhosDim, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_portrait_ph, 92);
    lv_obj_align(s_portrait_ph, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(s_portrait_ph, "头像未设置");

    s_lbl_name = make_label(portrait_card, &buddy_font_16, kPhos, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_letter_space(s_lbl_name, UI_LS_TITLE, 0);
    lv_obj_set_width(s_lbl_name, 98);
    lv_label_set_long_mode(s_lbl_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(s_lbl_name, 0, 156);
    s_lbl_sig = make_label(portrait_card, &buddy_font_16, kPhosDim, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_lbl_sig, 98);
    lv_obj_set_pos(s_lbl_sig, 0, 180);
    lv_label_set_long_mode(s_lbl_sig, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(s_lbl_name, "未设置姓名");
    lv_label_set_text(s_lbl_sig, "未设置签名");

    // ---- 时钟卡 (x114 y28 120×60):点阵 HH:MM,槽位手排,内容 114×54 ----
    lv_obj_t *clock_card = make_panel(s_screen, 114, 28, 120, 60, 0, true);
    static const int slot_x[5] = {0, 27, 55, 62, 89};
    for (int i = 0; i < 5; ++i) {
        s_clk_slots[i] = px_container(clock_card, slot_x[i], 9,
                                      i == 2 ? 4 : kDigitW, kClockH);
    }
    s_clk_colon[0] = px_block(s_clk_slots[2], 0, 8, 4, 4, kPhos);
    s_clk_colon[1] = px_block(s_clk_slots[2], 0, 23, 4, 4, kPhos);

    // ---- 日期卡 (x114 y92 120×46):日期 / 定位 pin + 城市 ----
    lv_obj_t *date_card = make_panel(s_screen, 114, 92, 120, 46, 0, true);
    s_lbl_date = make_label(date_card, &buddy_font_16, kPhos, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_lbl_date, 114);
    lv_obj_set_pos(s_lbl_date, 0, 2);

    px_block(date_card, 14, 22, 6, 6, kPhos);   // 定位 pin(与城市文字垂直居中)
    px_block(date_card, 16, 28, 2, 4, kPhos);
    s_lbl_city = make_label(date_card, &buddy_font_16, kPhosDim, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_lbl_city, 26, 18);
    lv_obj_set_width(s_lbl_city, 84);
    lv_label_set_long_mode(s_lbl_city, LV_LABEL_LONG_MODE_DOTS);

    // ---- 天气卡 (x114 y142 120×94):大温度 + 图标 / 短语 + 湿度 / 风向·风级 ----
    lv_obj_t *weather_card = make_panel(s_screen, 114, 142, 120, 94, 0, true);
    s_wicon_zone = px_container(weather_card, 4, 8, 22, 18);

    s_lbl_temp = make_label(weather_card, &lv_font_montserrat_20, kPhos,
                            LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(s_lbl_temp, 34, 8);
    lv_obj_set_width(s_lbl_temp, 76);

    s_lbl_wtext = make_label(weather_card, &buddy_font_16, kPhosDim, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_lbl_wtext, 4, 44);
    lv_obj_set_size(s_lbl_wtext, 40, 20);   // DOTS 必须定宽+定高,只定宽会折行
    lv_label_set_long_mode(s_lbl_wtext, LV_LABEL_LONG_MODE_DOTS);

    s_lbl_humidity = make_label(weather_card, &buddy_font_16, kPhosDim, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(s_lbl_humidity, 50, 44);
    lv_obj_set_size(s_lbl_humidity, 60, 20);   // 同上:定高保证单行,不再压到风向
    lv_label_set_long_mode(s_lbl_humidity, LV_LABEL_LONG_MODE_DOTS);

    px_block(weather_card, 4, 73, 12, 2, kPhosDim);       // 小风纹(三条错位风线)
    px_block(weather_card, 4, 77, 8, 2, kPhosDim);
    px_block(weather_card, 4, 81, 10, 2, kPhosDim);
    s_lbl_wind = make_label(weather_card, &buddy_font_16, kPhosDim, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_lbl_wind, 20, 69);
    lv_obj_set_size(s_lbl_wind, 90, 20);
    lv_label_set_long_mode(s_lbl_wind, LV_LABEL_LONG_MODE_DOTS);

    lv_label_set_text(s_lbl_temp, "--");
    lv_label_set_text(s_lbl_wtext, "");
    lv_label_set_text(s_lbl_humidity, "--");
    ui_pixel_label_set_text(s_lbl_wind, "风向 --");

    // ---- 底部艺术底图 (y240..320):A8 alpha 图 + 磷光绿 recolor,全宽出血 ----
    s_band_img = lv_image_create(s_screen);
    lv_image_set_src(s_band_img, &s_band_dsc[s_band_index]);
    lv_obj_set_pos(s_band_img, 0, 240);
    lv_obj_set_style_image_recolor(s_band_img, lv_color_hex(UI_AMBER), 0);

    // ---- 按键提示:黑条压在美术下缘 + 绿字(带 1px 黑投影) ----
    lv_obj_t *strip = px_block(s_screen, 0, 296, 240, 24, UI_BG);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    s_lbl_footer = make_label(s_screen, &buddy_font_16, kPhos, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_lbl_footer, kContentW);
    lv_obj_align(s_lbl_footer, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_label_set_long_mode(s_lbl_footer, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(s_lbl_footer, "上名片  下详情  确定应用");
}

constexpr uint16_t kJpegMcuHeight = 16;
constexpr std::size_t kJpegWorkSize = 4096;

struct ShareJpegContext {
    FILE *fp;
    uint16_t *stripe;
    uint16_t width;
    uint16_t height;
    bool color_pending;
    bool failed;
};

std::size_t share_jpeg_input(JDEC *decoder, uint8_t *buffer, std::size_t requested) {
    auto *context = static_cast<ShareJpegContext *>(decoder->device);
    if (!context || !context->fp) return 0;
    if (buffer) {
        return std::fread(buffer, 1, requested, context->fp);
    } else {
        return std::fseek(context->fp, static_cast<long>(requested), SEEK_CUR) == 0 ? requested : 0;
    }
}

int share_jpeg_output(JDEC *decoder, void *bitmap, JRECT *rectangle) {
    auto *context = static_cast<ShareJpegContext *>(decoder->device);
    if (!context || !bitmap || !rectangle || context->failed) return 0;

    const uint16_t block_width = rectangle->right - rectangle->left + 1;
    const uint16_t block_height = rectangle->bottom - rectangle->top + 1;
    if (rectangle->right >= context->width || rectangle->bottom >= context->height) {
        context->failed = true;
        return 0;
    }

    if (rectangle->left == 0 && context->color_pending) {
        if (bsp_display_io()) {
            esp_lcd_panel_io_tx_param(bsp_display_io(), 0x00, nullptr, 0);
        }
        context->color_pending = false;
    }

    uint16_t *target = context->stripe;
    if (!target) {
        context->failed = true;
        return 0;
    }

    // TJPGD 输出格式为 BGR888
    const auto *bgr = static_cast<const uint8_t *>(bitmap);
    for (uint16_t y = 0; y < block_height; ++y) {
        uint16_t *row = target + static_cast<std::size_t>(y) * context->width;
        for (uint16_t x = 0; x < block_width; ++x) {
            const std::size_t input = (static_cast<std::size_t>(y) * block_width + x) * 3U;
            const uint16_t color = static_cast<uint16_t>(
                ((bgr[input + 2] & 0xf8U) << 8U) |
                ((bgr[input + 1] & 0xfcU) << 3U) |
                (bgr[input] >> 3U));
            row[rectangle->left + x] = __builtin_bswap16(color);
        }
    }

    if (rectangle->right + 1U == context->width) {
        const uint16_t panel_top = rectangle->top;
        const uint16_t panel_bottom = rectangle->bottom + 1U;
        if (esp_lcd_panel_draw_bitmap(bsp_display_panel(), 0, panel_top,
                                      context->width, panel_bottom, target) != ESP_OK) {
            context->failed = true;
            return 0;
        }
        context->color_pending = true;
    }
    return 1;
}

bool home_show_share_image() {
    const char *path = home_profile_share_image_path();
    FILE *fp = std::fopen(path, "rb");
    if (!fp) {
        ESP_LOGW(kTag, "Cannot open share image: %s", path);
        return false;
    }

    auto *work = static_cast<uint8_t *>(std::malloc(kJpegWorkSize));
    auto *stripe = static_cast<uint16_t *>(
        std::malloc(HOME_SHARE_W * kJpegMcuHeight * sizeof(uint16_t)));
    if (!work || !stripe) {
        ESP_LOGE(kTag, "Alloc failed for share JPEG decoding");
        if (work) std::free(work);
        if (stripe) std::free(stripe);
        std::fclose(fp);
        return false;
    }

    ShareJpegContext context = {
        .fp = fp,
        .stripe = stripe,
        .width = HOME_SHARE_W,
        .height = HOME_SHARE_H,
        .color_pending = false,
        .failed = false,
    };

    JDEC decoder;
    JRESULT res = jd_prepare(&decoder, share_jpeg_input, work, kJpegWorkSize, &context);
    if (res != JDR_OK) {
        ESP_LOGW(kTag, "jd_prepare failed: %d", res);
        std::free(work);
        std::free(stripe);
        std::fclose(fp);
        return false;
    }

    if (decoder.width > HOME_SHARE_W || decoder.height > HOME_SHARE_H) {
        ESP_LOGW(kTag, "Share image dimensions (%ux%u) exceed max (%ux%u)",
                 decoder.width, decoder.height, HOME_SHARE_W, HOME_SHARE_H);
        std::free(work);
        std::free(stripe);
        std::fclose(fp);
        return false;
    }
    context.width = decoder.width;
    context.height = decoder.height;

    res = jd_decomp(&decoder, share_jpeg_output, 0);
    std::fclose(fp);

    if (context.color_pending && bsp_display_io()) {
        esp_lcd_panel_io_tx_param(bsp_display_io(), 0x00, nullptr, 0);
    }

    std::free(work);
    std::free(stripe);

    if (res != JDR_OK || context.failed) {
        ESP_LOGW(kTag, "jd_decomp failed: %d (failed=%d)", res, context.failed);
        return false;
    }

    ESP_LOGI(kTag, "Wallet card displayed: %ux%u color JPEG", decoder.width, decoder.height);
    return true;
}

void toggle_share_view() {
    if (s_share_visible) {
        bsp_lvgl_lock(-1);
        s_share_visible = false;
        lv_display_t *disp = lv_display_get_default();
        if (disp) lv_display_enable_invalidation(disp, true);
        if (s_screen) lv_obj_invalidate(s_screen);
        bsp_lvgl_unlock();
        return;
    }

    if (!home_profile_has_share_image()) {
        ESP_LOGW(kTag, "UP: no valid wallet card file");
        // 尚未上传随身名片时保留原有的手动天气刷新能力。
        s_force_weather.store(true);
        return;
    }

    bsp_lvgl_lock(-1);
    s_share_visible = true;
    lv_display_t *disp = lv_display_get_default();
    if (disp) lv_display_enable_invalidation(disp, false);
    bsp_lvgl_unlock();

    // 在锁外执行文件读取与分块直推屏幕
    if (!home_show_share_image()) {
        bsp_lvgl_lock(-1);
        s_share_visible = false;
        if (disp) lv_display_enable_invalidation(disp, true);
        if (s_screen) lv_obj_invalidate(s_screen);
        bsp_lvgl_unlock();
    }
}

// 长按上键:循环切换底部艺术底图(城市夜景 → 山湖月色;将来加入用户上传槽)。
void band_art_next(void) {
    s_band_index = (s_band_index + 1) % 2;
    if (s_band_img) {
        lv_image_set_src(s_band_img, &s_band_dsc[s_band_index]);
    }
}

void home_task(void *arg) {
    (void)arg;
    const int64_t start_ms = esp_timer_get_time() / 1000;
    int64_t last_weather_ms = 0;
    int64_t upload_started_ms = 0;
    bool upload_ps_high = false; // 上传模式是否已切 BALANCED 省电档
    bool clock_on = false;
    uint32_t last_profile_ver = home_profile_version();
    uint32_t last_weather_ver = home_weather_version();
    home_profile_t prof;
    home_profile_get(&prof);

    while (s_home_active.load()) {
        const int64_t now_ms = esp_timer_get_time() / 1000;

        if (bsp_lvgl_lock(1000)) {
            if (s_ui_alive.load() && !s_share_visible) {
                update_clock_labels();
                update_battery_label();
                update_wind_line();
                // 底部行每秒重算:上传模式下它会把刚拿到的 IP 补进 URL,
                // 避免"Wi-Fi 刚连上还没拿到地址"时显示成空的 http://。
                update_footer_label();
                weather_icon_spin_tick();
            }
            bsp_lvgl_unlock();
        }

        // Wi-Fi 就绪才跑 SNTP;断开即停,避免 lwIP 一直重试。
        const bool connected = WifiManager::GetInstance().IsConnected();
        if (connected && !clock_on) {
            home_clock_start();
            clock_on = true;
        } else if (!connected && clock_on) {
            home_clock_stop();
            clock_on = false;
        }

        // 资料(含头像)变化:先在锁外做文件 IO,再持锁更新 UI。
        const uint32_t profile_ver = home_profile_version();
        if (profile_ver != last_profile_ver) {
            last_profile_ver = profile_ver;
            home_profile_get(&prof);
            const bool has = home_profile_has_avatar();
            esp_err_t err = ESP_FAIL;
            if (has) err = home_profile_read_avatar(s_portrait_1bpp, sizeof(s_portrait_1bpp));
            if (bsp_lvgl_lock(1000)) {
                if (s_ui_alive.load()) {
                    apply_profile(&prof);
                    apply_avatar(err == ESP_OK);
                }
                bsp_lvgl_unlock();
            }
        }

        // 天气:启动 15s 后首次,之后每 30 分钟;UP 键可强制一次。网络阻塞在本任务内。
        home_profile_get(&prof);
        const bool want_weather = connected && prof.lat[0] != '\0' &&
                                  (s_force_weather.load() ||
                                   (last_weather_ms == 0 ? (now_ms - start_ms >= kWeatherFirstDelayMs)
                                                         : (now_ms - last_weather_ms >= kWeatherPeriodMs)));
        if (want_weather) {
            s_force_weather.store(false);
            last_weather_ms = now_ms;
            // 拉取期间天气卡左侧转四点 spinner(每秒转一格),结束后按结果换图标
            s_weather_fetching = true;
            if (bsp_lvgl_lock(1000)) {
                refresh_weather_ui();
                bsp_lvgl_unlock();
            }
            home_weather_refresh(prof.lat, prof.lon);   // 阻塞数秒,不持锁
            s_weather_fetching = false;
            if (!s_home_active.load()) break;
            if (home_weather_version() != last_weather_ver) {
                last_weather_ver = home_weather_version();
            }
            // 无论成败都要刷新一次:成功换天气图标,失败把 spinner 撤下来
            if (bsp_lvgl_lock(1000)) {
                refresh_weather_ui();
                bsp_lvgl_unlock();
            }
        }

        // 上传模式期间恢复即时下行(手机/设备往返都要快),结束时回到待机省电档。
        // 用电平变化检测,按键开启与 10 分钟自动超时两条路径都覆盖。
        const bool upload_active = home_upload_active();
        if (upload_active != upload_ps_high) {
            upload_ps_high = upload_active;
            WifiManager::GetInstance().SetPowerSaveLevel(
                upload_active ? WifiPowerSaveLevel::BALANCED : WifiPowerSaveLevel::LOW_POWER);
        }

        // 上传模式 10 分钟自动关闭,避免 httpd 长期占着内存。
        if (home_upload_active()) {
            if (upload_started_ms == 0) {
                upload_started_ms = now_ms;
            } else if (now_ms - upload_started_ms >= kUploadTimeoutMs) {
                home_upload_stop();
                upload_started_ms = 0;
            }
        } else {
            upload_started_ms = 0;
        }

        // 1s 周期拆成 10×100ms:stop() 后 ≤100ms 内退出任务,应用切换不再
        // 等满一秒 —— 切入小智/收音机时的过渡黑屏时间显著缩短。
        for (int i = 0; i < 10 && s_home_active.load(); ++i) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    s_task_handle = nullptr;
    vTaskDelete(NULL);
}

void home_init() {
    home_profile_init();
    home_weather_init();
    // 只有 voice 应用调过 time_sync_init();首页自己调一次保证时区已载入(幂等 NVS 读)。
    time_sync_init();

    // LVGL 9 的 header 是位域结构,逐字段赋值比指定初始化器更稳(避免位域初始化顺序问题)。
    // 人像显示用 A8(alpha):1bpp 打包展开后以 recolor 绿填充。
    s_portrait_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_portrait_dsc.header.cf = LV_COLOR_FORMAT_A8;
    s_portrait_dsc.header.flags = 0;
    s_portrait_dsc.header.w = HOME_PORTRAIT_W;
    s_portrait_dsc.header.h = HOME_PORTRAIT_H;
    s_portrait_dsc.header.stride = HOME_PORTRAIT_W;
    s_portrait_dsc.data_size = HOME_PORTRAIT_W * HOME_PORTRAIT_H;
    s_portrait_dsc.data = s_portrait_a8;

    // 内置底图(A8,数据在 flash,零 RAM 占用)
    const uint8_t *band_data[2] = {kBandArtCity, kBandArtMountain};
    for (int i = 0; i < 2; ++i) {
        s_band_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_band_dsc[i].header.cf = LV_COLOR_FORMAT_A8;
        s_band_dsc[i].header.flags = 0;
        s_band_dsc[i].header.w = BAND_ART_W;
        s_band_dsc[i].header.h = BAND_ART_H;
        s_band_dsc[i].header.stride = BAND_ART_W;
        s_band_dsc[i].data_size = BAND_ART_W * BAND_ART_H;
        s_band_dsc[i].data = band_data[i];
    }

    ESP_LOGI(kTag, "Home app subsystems ready");
}

void home_start() {
    ESP_LOGI(kTag, "Starting Home app");
    s_home_active.store(true);
    s_ui_alive.store(true);
    s_detail_mode = 0;
    s_share_visible = false;

    // 首屏资料与人像在持锁前读完:SPIFFS 首次挂载可达几十毫秒,不应阻塞 LVGL 任务。
    // A8 展开缓冲是堆分配的(见 s_portrait_a8 说明),启动时申请、停止时归还。
    if (s_portrait_a8 == nullptr) {
        s_portrait_a8 = static_cast<uint8_t *>(malloc(HOME_PORTRAIT_W * HOME_PORTRAIT_H));
        if (s_portrait_a8 != nullptr) {
            s_portrait_dsc.data = s_portrait_a8;
        } else {
            ESP_LOGW(kTag, "portrait A8 buffer alloc failed; avatar disabled");
        }
    }

    home_profile_t prof;
    home_profile_get(&prof);
    const bool has_avatar = home_profile_has_avatar() && s_portrait_a8 != nullptr;
    const esp_err_t avatar_err =
        has_avatar ? home_profile_read_avatar(s_portrait_1bpp, sizeof(s_portrait_1bpp))
                   : ESP_FAIL;

    bsp_lvgl_lock(-1);
    if (!s_screen) create_ui();
    apply_profile(&prof);
    apply_avatar(avatar_err == ESP_OK);
    update_clock_labels();
    update_battery_label();
    update_wind_line();
    update_footer_label();
    lv_scr_load(s_screen);
    bsp_lvgl_unlock();

    // 栈给 8192 而不是文档里的 4096:本任务内会跑 HTTPS(mbedTLS 握手调用链很深),
    // 4KB 栈在证书校验阶段有溢出风险;另外天气图标走 LVGL TJPEG decoder_info
    // (文件源),它把 4KB 的 TJPGD_WORKBUFF_SIZE 放在栈上(lv_tjpgd.c:110),
    // 6144 在换工具链后实测爆栈(Stack protection fault,2026-09-13)。
    BaseType_t created = xTaskCreate(home_task, "home_app", 8192, NULL, 4, &s_task_handle);
    if (created != pdPASS) {
        s_task_handle = nullptr;
        ESP_LOGE(kTag, "Failed to create home task; free heap: %u KB",
                 (unsigned)(esp_get_free_heap_size() / 1024));
        return;
    }
    ESP_LOGI(kTag, "Home app started; free heap: %u KB",
             (unsigned)(esp_get_free_heap_size() / 1024));
}

void home_stop() {
    ESP_LOGI(kTag, "Stopping Home app");
    s_home_active.store(false);
    // 先撤掉 UI 开关:home_task 持锁刷新前会看它,避免"删屏后任务还在写标签"。
    s_ui_alive.store(false);

    // 2026-09-13 修复(C3):原来是 while (s_task_handle) 无上界等待,而本函数
    // 跑在 input_task 里 —— 一旦被等任务卡住(例如卡在 LVGL 锁上),按键就
    // 永久没反应。改成 2s 上界 + 超时告警;即使超时,因为 s_ui_alive 已经
    // 置 false,残留任务也不会再碰已删除的界面对象。
    int wait_ms = 0;
    while (s_task_handle != nullptr && wait_ms < 2500) {
        vTaskDelay(pdMS_TO_TICKS(20));
        wait_ms += 20;
    }
    if (s_task_handle != nullptr) {
        ESP_LOGW(kTag, "home task did not exit in 2.5s; deleting task");
        TaskHandle_t th = s_task_handle;
        s_task_handle = nullptr;
        vTaskDelete(th);
    }

    home_upload_stop();
    home_clock_stop();

    bsp_lvgl_lock(-1);
    if (s_screen) {
        lv_obj_delete(s_screen);
    }
    s_screen = nullptr;
    s_avatar_img = nullptr;
    s_portrait_ph = nullptr;
    s_lbl_date = nullptr;
    for (lv_obj_t *&bar : s_wifi_bars) bar = nullptr;
    s_batt = {nullptr, nullptr};
    s_band_img = nullptr;
    for (lv_obj_t *&slot : s_clk_slots) slot = nullptr;
    for (lv_obj_t *&dot : s_clk_colon) dot = nullptr;
    std::memset(s_clk_slot_char, 0, sizeof(s_clk_slot_char));
    s_lbl_name = nullptr;
    s_lbl_sig = nullptr;
    s_wicon_zone = nullptr;
    // 屏幕删除时天气图标块已随之销毁,这里只清登记状态,不能再 lv_obj_delete
    for (lv_obj_t *&o : s_wicon_objs) o = nullptr;
    s_wicon_n = 0;
    for (lv_obj_t *&b : s_spin) b = nullptr;
    s_wicon_spinning = false;
    s_lbl_city = nullptr;
    s_lbl_wtext = nullptr;
    s_lbl_temp = nullptr;
    s_lbl_humidity = nullptr;
    s_lbl_wind = nullptr;
    s_lbl_footer = nullptr;
    if (s_share_visible) {
        lv_display_t *disp = lv_display_get_default();
        if (disp) lv_display_enable_invalidation(disp, true);
        s_share_visible = false;
    }
    bsp_lvgl_unlock();

    // 屏已删、任务已退,人像 A8 缓冲可以还给堆了 —— 其它应用(小智)正等着
    // 这块连续内存开 Opus 编码器。dsc.data 一并清掉,避免悬挂引用。
    if (s_portrait_a8 != nullptr) {
        free(s_portrait_a8);
        s_portrait_a8 = nullptr;
        s_portrait_dsc.data = nullptr;
    }

    ESP_LOGI(kTag, "Home app stopped. Free heap: %u KB",
             (unsigned)(esp_get_free_heap_size() / 1024));
}

void home_on_key(uint8_t btn, app_btn_event_t event, uint16_t mv) {
    (void)mv;

    // 手机设置与首次开机共用同一个热点入口。
    if (btn == BSP_BTN_DOWN && event == BTN_EVT_LONG) {
        app_registry_switch_to(APP_ID_WIFI);
        return;
    }

    // 长按 UP = 切换底部艺术底图。
    if (btn == BSP_BTN_UP && event == BTN_EVT_LONG) {
        if (s_share_visible) return;
        bsp_lvgl_lock(-1);
        band_art_next();
        bsp_lvgl_unlock();
        return;
    }

    if (event != BTN_EVT_CLICK) return;

    // OK 短按 = 打开应用板块。
    if (btn == BSP_BTN_OK) {
        if (s_share_visible) {
            toggle_share_view();
            return;
        }
        ESP_LOGI(kTag, "Opening app drawer from Home");
        app_registry_switch_to(APP_ID_LAUNCHER);
        return;
    }

    if (btn == BSP_BTN_UP) {
        toggle_share_view();
        return;
    } else if (btn == BSP_BTN_DOWN) {
        if (s_share_visible) {
            toggle_share_view();
            return;
        }
        // 底部行三态轮换:操作提示 → 上传提示 → 网络/天气详情。
        s_detail_mode = static_cast<uint8_t>((s_detail_mode + 1) % 3);
    } else {
        return;
    }

    bsp_lvgl_lock(-1);
    update_footer_label();
    bsp_lvgl_unlock();
}

}  // namespace

const passport_app_t g_home_app = {
    .id = APP_ID_HOME,
    .name = "首页",
    .en_name = "HOME",
    .desc = "时钟 · 天气 · 名片 · 上传",
    .tag = "Wi-Fi",
    .theme_color = 0xDDB36C,
    .init = home_init,
    .start = home_start,
    .stop = home_stop,
    .on_key = home_on_key,
};
