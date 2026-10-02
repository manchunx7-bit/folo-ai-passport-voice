#include "launcher/wifi_setup.h"
#include "launcher/app_registry.h"
#include "apps/home/home_upload.h"
#include "apps/home/home_profile.h"
#include "wifi_manager.h"
#include "ssid_manager.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "app_fonts.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

namespace {
std::atomic<bool> s_required{false};
std::atomic<bool> s_active{false};
std::atomic<bool> s_completing{false};
lv_obj_t *s_screen = nullptr;
lv_obj_t *s_status = nullptr;

lv_obj_t *line(const char *text, int y, uint32_t color, const lv_font_t *font = &buddy_font_16) {
    auto *label = ui_pixel_label(s_screen, text, font, color);
    lv_obj_set_pos(label, 14, y);
    lv_obj_set_width(label, 212);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}
}

void wifi_setup_require_initial(void) { s_required = true; }
bool wifi_setup_can_leave(void) { return !s_required.load() && !home_upload_busy(); }
void wifi_setup_init(void) {}

void wifi_setup_start(void) {
    s_active = true;
    home_profile_init();
    if (SsidManager::GetInstance().GetSsidList().empty()) s_required = true;
    auto &wifi = WifiManager::GetInstance();
    wifi.StartConfigAp(true); // DNS/AP only; one shared HTTP server owns the full flow.
    const auto err = home_upload_start();
    bsp_lvgl_lock(-1);
    s_screen = lv_obj_create(nullptr);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_pixel_background(s_screen);
    ui_pixel_title_plate(s_screen, 10, "手机设置", &ui_font_20);
    line(s_required ? "欢迎使用 AI 护照" : "用手机完成设置", 44, UI_TEXT);
    line("1  连接设备热点", 80, UI_TEXT_DIM);
    line(wifi.GetApSsid().c_str(), 106, UI_AMBER, &lv_font_montserrat_14);
    line("2  手机浏览器打开", 146, UI_TEXT_DIM);
    line("http://192.168.4.1", 172, UI_AMBER, &ui_font_20);
    s_status = line(err == ESP_OK ? "配网、资料和名片，一次完成" : "服务未启动，请按确定重试", 216, UI_TEXT);
    ui_pixel_footer(s_screen, "提示无互联网时选保留",
                    s_required ? "请先在手机完成联网" : "长按确定返回首页", &buddy_font_16);
    lv_screen_load(s_screen);
    bsp_lvgl_unlock();
    app_registry_keep_awake();
}

void wifi_setup_stop(void) {
    s_active = false;
    home_upload_stop();
    WifiManager::GetInstance().StopConfigAp();
    if (!SsidManager::GetInstance().GetSsidList().empty()) WifiManager::GetInstance().StartStation();
    bsp_lvgl_lock(-1);
    if (s_screen) lv_obj_delete(s_screen);
    s_screen = s_status = nullptr;
    bsp_lvgl_unlock();
}

void wifi_setup_on_key(uint8_t btn, app_btn_event_t event, uint16_t) {
    if (btn == BSP_BTN_OK && event == BTN_EVT_CLICK && !home_upload_active()) {
        const auto err = home_upload_start();
        bsp_lvgl_lock(-1);
        ui_pixel_label_set_text(s_status, err == ESP_OK ? "请在手机浏览器刷新页面" : "启动失败，请重启设备");
        bsp_lvgl_unlock();
    }
}

void wifi_setup_tick(void) {
    if (!s_active.load()) return;
    app_registry_keep_awake();
    if (home_upload_finished()) {
        // Home's JPEG decoder needs >4 KB stack; never switch from sys_mon's 2560 B stack.
        if (!s_completing.exchange(true)) {
            if (xTaskCreate([](void *) {
                s_required = false;
                if (s_active.load()) app_registry_switch_to(APP_ID_HOME);
                s_completing = false;
                vTaskDelete(nullptr);
            }, "setup_done", 8192, nullptr, 4, nullptr) != pdPASS) s_completing = false;
        }
        return;
    }
    if (bsp_lvgl_lock(50)) {
        if (s_status) ui_pixel_label_set_text(s_status, home_upload_busy() ? "正在连接，请保持热点连接" :
                    home_upload_network_verified() ? "联网成功，请在手机完成设置" :
                    home_upload_active() ? "配网、资料和名片，一次完成" : "服务未启动，请按确定重试");
        bsp_lvgl_unlock();
    }
}

const passport_app_t g_wifi_app = {
    .id = APP_ID_WIFI, .name = "手机设置", .en_name = "PHONE SETUP",
    .desc = "配网、资料与名片", .tag = "SYSTEM", .theme_color = 0x181B1C,
    .init = wifi_setup_init, .start = wifi_setup_start,
    .stop = wifi_setup_stop, .on_key = wifi_setup_on_key,
};
