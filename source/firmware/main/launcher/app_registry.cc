#include "launcher/app_registry.h"
#include "launcher/launcher.h"
#include "launcher/wifi_setup.h"
#include "launcher/wake_gate.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "lvgl.h"
#include "ui_pixel.h"
#include "app_fonts.h"
#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <atomic>
#include <vector>

static const char *TAG = "app_registry";

// 外部 App 实例前向声明
extern const passport_app_t g_radio_app;
extern const passport_app_t g_voice_app;
extern const passport_app_t g_xiaozhi_app;
extern const passport_app_t g_wifi_app;
extern const passport_app_t g_settings_app;
extern const passport_app_t g_home_app;
extern const passport_app_t g_game_app;
extern const passport_app_t g_tetris_app;
extern const passport_app_t g_stopwatch_app;
extern const passport_app_t g_sound_jump_app;

namespace {

// 应用板块(Launcher 轮播)里展示的应用。
// 首页(APP_ID_HOME)**不在这里** —— 它是底座而非一张卡,改由
// app_registry_get_by_id() 单独寻址,这样 switch_to(APP_ID_HOME) 才不会找不到而黑屏。
// AI设备(AI设备/Codex 陪伴精灵)已于 2026-09-12 移除,见 docs/app-review-2026-09-12.md。
const passport_app_t *s_apps[] = {
    &g_radio_app,
    &g_voice_app,
    &g_xiaozhi_app,
    &g_game_app,
    &g_sound_jump_app,
    &g_tetris_app,
    &g_stopwatch_app,
    &g_settings_app,   // 设置放最后(应用惯用收尾位置)
};

constexpr size_t kAppCount = sizeof(s_apps) / sizeof(s_apps[0]);
std::atomic<passport_app_id_t> s_active_id{APP_ID_LAUNCHER};
wake_gate_t s_wake_gate{};
bool s_initialized[APP_ID_COUNT] = {};

// 2026-09-13:switch_to() 会被三个不同任务调用(input_task 按键/长按、
// main.cc 的串口命令任务、wifi_setup 的 connect_worker),而它内部是
// "stop 旧 App → start 新 App" 的多步序列 —— 并发执行会出现双 start、
// 双建任务、s_active_id 撕裂。这里用互斥量把整个切换串行化。
// 取锁用 3s 超时而不是死等:宁可丢一次切换,也不能把调用方永久挂住
// (参考 A2:C3 上任何无上界等待最终都会变成"按键没反应")。
SemaphoreHandle_t s_switch_mutex = nullptr;

struct SwitchGuard {
    bool owned = false;
    bool try_take() {
        if (!s_switch_mutex) return false;
        owned = xSemaphoreTakeRecursive(s_switch_mutex, pdMS_TO_TICKS(3000)) == pdTRUE;
        return owned;
    }
    ~SwitchGuard() {
        if (owned && s_switch_mutex) xSemaphoreGiveRecursive(s_switch_mutex);
    }
};

// 按前台应用选择 Wi-Fi 省电档:流媒体/语音需要即时下行,保持 MIN modem sleep;
// 桌面/首页/设置等待机前台放宽到 MAX modem sleep(下行延迟可到 ~1s,对
// 天气/SNTP/上传服务无感),是待机电流的最大单项优化。
WifiPowerSaveLevel power_save_level_for(passport_app_id_t id) {
    switch (id) {
        case APP_ID_RADIO:
        case APP_ID_VOICE:
        case APP_ID_XIAOZHI:
            return WifiPowerSaveLevel::BALANCED;
        default:
            return WifiPowerSaveLevel::LOW_POWER;
    }
}

// 息屏与背光管理
std::atomic<bool> s_screen_on{true};
std::atomic<int64_t> s_last_activity_us{0};
std::atomic<uint16_t> s_screen_timeout_seconds{60};

// 长按 OK 强制返回标志
bool s_ok_long_triggered = false;
lv_obj_t *s_pending_transition = nullptr;
lv_timer_t *s_transition_delay = nullptr;

void cancel_transition_delay() {
    if (s_transition_delay) {
        lv_timer_delete(s_transition_delay);
        s_transition_delay = nullptr;
    }
}

lv_obj_t *show_transition_screen(const char *target) {
    if (!bsp_lvgl_lock(500)) return nullptr;
    cancel_transition_delay();
    lv_obj_t *screen = lv_obj_create(nullptr);
    if (screen) {
        // 过渡屏用中性黑:首页已是黑底磷光风,亮蓝色闪过会非常突兀;
        // 黑色在两种主题(像素风/磷光风)下都读作"翻页"而非"变色"。
        lv_obj_set_style_bg_color(screen, lv_color_hex(UI_BG), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *label = ui_pixel_label(screen, target, &ui_font_20, UI_TEXT);
        lv_obj_set_width(label, 216);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, -14);
        lv_obj_t *hint = ui_pixel_label(label, "正在打开", &buddy_font_16, UI_TEXT_DIM);
        lv_obj_align(hint, LV_ALIGN_OUT_BOTTOM_MID, 0, 14);
        lv_obj_add_flag(label, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        // Fast transitions stay quiet. Slow audio/network teardown gets a real destination.
        s_transition_delay = lv_timer_create([](lv_timer_t *timer) {
            lv_obj_remove_flag(static_cast<lv_obj_t *>(lv_timer_get_user_data(timer)), LV_OBJ_FLAG_HIDDEN);
            s_transition_delay = nullptr;
        }, 160, label);
        if (s_transition_delay) lv_timer_set_repeat_count(s_transition_delay, 1);
        lv_screen_load(screen);
        // 异步 UI（Codex）可能把上一次过渡页保留到下一次切换；此刻已有
        // 新活动页，可以安全回收旧过渡页。
        if (s_pending_transition && s_pending_transition != screen) {
            lv_obj_delete(s_pending_transition);
            s_pending_transition = nullptr;
        }
    }
    bsp_lvgl_unlock();
    return screen;
}

void release_transition_screen(lv_obj_t *screen) {
    if (!screen || !bsp_lvgl_lock(500)) return;
    // 只有目标 App 已经加载了自己的 screen 才释放过渡页。这样即使目标页
    // 创建失败，LVGL 也不会留下指向已释放活动 screen 的悬空指针。
    if (lv_screen_active() != screen) {
        cancel_transition_delay();
        lv_obj_delete(screen);
    } else {
        s_pending_transition = screen;
        ESP_LOGI(TAG, "Target screen is asynchronous; transition cleanup deferred");
    }
    bsp_lvgl_unlock();
}

} // namespace

void app_registry_init(void) {
    ESP_LOGI(TAG, "Initializing app registry with %u apps...", (unsigned)kAppCount);
    if (!s_switch_mutex) s_switch_mutex = xSemaphoreCreateRecursiveMutex();
    s_last_activity_us.store(esp_timer_get_time(), std::memory_order_release);
    s_active_id = APP_ID_LAUNCHER;
    // 不在这里初始化/加载 Launcher 界面:开机直接由 main 走开机动画后进首页,
    // 提前加载会让应用页在开机时闪现一两帧。抽屉进入时 launcher_ui_show() 自建。
}

size_t app_registry_get_count(void) {
    return kAppCount;
}

const passport_app_t *app_registry_get_by_index(size_t index) {
    if (index >= kAppCount) return nullptr;
    return s_apps[index];
}

const passport_app_t *app_registry_get_by_id(passport_app_id_t id) {
    // 首页不在 s_apps 轮播列表里,但仍必须能被 switch_to() 直接寻址。
    // 少了这个特判,切回首页会因为找不到 app 实例而停在过渡屏(黑屏)。
    if (id == APP_ID_HOME) return &g_home_app;
    if (id == APP_ID_WIFI) return &g_wifi_app; // hidden system setup, never a launcher card
    for (size_t i = 0; i < kAppCount; ++i) {
        if (s_apps[i] && s_apps[i]->id == id) {
            return s_apps[i];
        }
    }
    return nullptr;
}

passport_app_id_t app_registry_get_active(void) {
    return s_active_id;
}

void app_registry_switch_to(passport_app_id_t id) {
    if (s_active_id == id) return;

    // 串行化:见 s_switch_mutex 注释。取不到锁直接放弃本次切换并留日志,
    // 不做无上界等待。
    SwitchGuard guard;
    if (!guard.try_take()) {
        ESP_LOGE(TAG, "switch_to(%d): registry busy; switch dropped", (int)id);
        return;
    }

    // Another caller may have completed this same switch while we waited.
    if (s_active_id == id) return;

    if (id != APP_ID_WIFI && !wifi_setup_can_leave()) return;

    // 目标 App 必须真实存在,否则会卡在过渡屏(黑屏):不启动任何 App 时,
    // release_transition_screen() 会把过渡屏留作活动屏,用户就永远停在黑屏上。
    // 这个守卫在 2026-09-12 移除 AI设备 模块后加,防止残留 id 再次踩坑。
    const passport_app_t *next = app_registry_get_by_id(id);
    if (id != APP_ID_LAUNCHER && next == nullptr) {
        ESP_LOGE(TAG, "switch_to(%d): no app registered for this id; switch ignored", (int)id);
        return;
    }

    ESP_LOGI(TAG, "Switching app: %d -> %d", (int)s_active_id.load(), id);

    // 永远先切到一个极小的过渡 screen，再销毁当前页面。直接删除 LVGL 的
    // active screen 会让 display 保留悬空指针，下一次 layout 可形成父链死循环。
    const int64_t switch_started_us = esp_timer_get_time();
    lv_obj_t *transition = show_transition_screen(next ? next->name : "应用");
    if (!transition) {
        ESP_LOGE(TAG, "Unable to allocate transition screen; app switch cancelled safely");
        return;
    }

    // 1. 退出前一个 App 并清理资源
    if (s_active_id != APP_ID_LAUNCHER) {
        const passport_app_t *prev = app_registry_get_by_id(s_active_id);
        if (prev && prev->stop) {
            ESP_LOGI(TAG, "Stopping app: %s", prev->name);
            prev->stop();
        }
    } else {
        launcher_ui_hide();
    }

    // 2. 打印当前可用空闲内存，确认清理效果
    uint32_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Free heap after app stop: %u KB", (unsigned)(free_heap / 1024));

    s_active_id = id;
    // manager 未初始化时只记忆档位,StartStation 后自动生效(见 wifi_manager)
    WifiManager::GetInstance().SetPowerSaveLevel(power_save_level_for(id));

    // 3. 启动新目标
    if (id == APP_ID_LAUNCHER) {
        launcher_ui_show();
    } else if (next) {
        ESP_LOGI(TAG, "Starting app: %s", next->name);
        if (next->init && !s_initialized[id]) {
            next->init();
            s_initialized[id] = true;
        }
        if (next->start) next->start();
    }
    release_transition_screen(transition);
    ESP_LOGI(TAG, "Transition ready: app=%d elapsed_ms=%lld free=%u", id,
             (long long)((esp_timer_get_time() - switch_started_us) / 1000),
             (unsigned)esp_get_free_heap_size());
}

void app_registry_dispatch_key(uint8_t btn, app_btn_event_t event, uint16_t mv) {
    // Hold the same lifecycle lock as stop/start while calling app->on_key.
    // Recursive because a key handler may itself request an app switch.
    SwitchGuard guard;
    if (!guard.try_take()) return;
    int64_t now = esp_timer_get_time();
    ESP_LOGI(TAG, "Dispatch Key: btn=%d, evt=%d, mv=%u, cur_active=%d", btn, event, mv, (int)s_active_id.load());

    if (wake_gate_consume(&s_wake_gate, event == BTN_EVT_PRESS)) return;

    // 1. 熄屏唤醒护栏：如果屏幕已休眠，第一按只点亮屏幕，不透传给业务
    if (!s_screen_on.load(std::memory_order_acquire)) {
        if (event == BTN_EVT_PRESS || event == BTN_EVT_CLICK) {
            // 先唤醒面板(SLPOUT+DISPON,内部含 120ms 稳定等待),再开背光,
            // 恢复 LVGL 刷新并补一次整屏重绘(睡眠期间的刷屏被面板丢弃过)。
            wake_gate_arm(&s_wake_gate);
            if (!bsp_lvgl_lock(500)) return;
            bsp_display_power(true);
            bsp_display_backlight(100);
            s_screen_on.store(true, std::memory_order_release);
            s_last_activity_us.store(now, std::memory_order_release);
            {
                lv_timer_t *refr = lv_display_get_refr_timer(lv_display_get_default());
                if (refr) lv_timer_resume(refr);
                lv_obj_t *scr = lv_screen_active();
                if (scr) lv_obj_invalidate(scr);
                bsp_lvgl_unlock();
            }
            ESP_LOGI(TAG, "Screen woken up by key press");
            return;
        }
    }

    s_last_activity_us.store(now, std::memory_order_release);

    // 2. 全局拦截:长按 OK = 整机统一的"返回",沿层级上移:
    //    子应用 → 应用板块 → 首页。首页已是顶层,长按无动作。
    //    (后续所有 App 的交互设计都遵循这条语义,不再各自定义长按 OK。)
    if (btn == BSP_BTN_OK && event == BTN_EVT_LONG) {
        if (s_active_id == APP_ID_WIFI) {
            s_ok_long_triggered = true;
            if (wifi_setup_can_leave()) app_registry_switch_to(APP_ID_HOME);
            return;
        }
        if (s_active_id == APP_ID_LAUNCHER) {
            ESP_LOGI(TAG, "Back: Launcher -> Home");
            s_ok_long_triggered = true;
            app_registry_switch_to(APP_ID_HOME);
            return;
        }
        if (s_active_id != APP_ID_HOME) {
            ESP_LOGI(TAG, "Back: App -> Launcher");
            s_ok_long_triggered = true;
            app_registry_switch_to(APP_ID_LAUNCHER);
            return;
        }
        // 首页是顶层:不拦不吞,首页 on_key 本身不消费长按 OK。
    }
    if (btn == BSP_BTN_OK && (event == BTN_EVT_RELEASE || event == BTN_EVT_LONG_UP)) {
        if (s_ok_long_triggered) {
            s_ok_long_triggered = false;
            return;
        }
    }

    // 3. 事件分发
    if (s_active_id == APP_ID_LAUNCHER) {
        launcher_ui_on_key(btn, event);
    } else {
        const passport_app_t *app = app_registry_get_by_id(s_active_id);
        if (app && app->on_key) {
            app->on_key(btn, event, mv);
        }
    }
}

void app_registry_tick(void) {
    if (bsp_lvgl_lock(50)) {
        if (s_pending_transition && lv_screen_active() != s_pending_transition) {
            cancel_transition_delay();
            lv_obj_delete(s_pending_transition);
            s_pending_transition = nullptr;
        }
        bsp_lvgl_unlock();
    }

    const uint16_t timeout = s_screen_timeout_seconds.load(std::memory_order_acquire);
    if (!timeout || !s_screen_on.load(std::memory_order_acquire)) return;

    const int64_t now = esp_timer_get_time();
    const int64_t last = s_last_activity_us.load(std::memory_order_acquire);
    if (now - last >= static_cast<int64_t>(timeout) * 1000000LL) {
        // 持 LVGL 锁做息屏:与刷屏事务串行,避免 DISPOFF/SLPIN 插进 DMA 传输中间;
        // 睡眠期间顺手暂停 LVGL 刷新(往睡着的面板刷数据既费电又无意义)。
        if (bsp_lvgl_lock(200)) {
            const int64_t latest = s_last_activity_us.load(std::memory_order_acquire);
            if (esp_timer_get_time() - latest < static_cast<int64_t>(timeout) * 1000000LL) {
                bsp_lvgl_unlock();
                return;
            }
            bsp_display_backlight(0);
            bsp_display_power(false); // DISPOFF+SLPIN 面板真睡眠,只关背光会有内容残影
            lv_timer_t *refr = lv_display_get_refr_timer(lv_display_get_default());
            if (refr) lv_timer_pause(refr);
            s_screen_on.store(false, std::memory_order_release);
            bsp_lvgl_unlock();
            ESP_LOGI(TAG, "Screen blanked after %u seconds", (unsigned)timeout);
        }
    }
}

void app_registry_keep_awake(void) {
    // 只续期不唤醒:屏幕已熄灭时刷新计时也无副作用(仍是熄灭态),
    // 下一次真正的按键依旧走"第一按仅唤醒"护栏。
    s_last_activity_us.store(esp_timer_get_time(), std::memory_order_release);
}

void app_registry_set_screen_timeout(uint16_t seconds) {
    s_screen_timeout_seconds.store(seconds, std::memory_order_release);
    s_last_activity_us.store(esp_timer_get_time(), std::memory_order_release);
}

uint16_t app_registry_get_screen_timeout(void) {
    return s_screen_timeout_seconds.load(std::memory_order_acquire);
}
