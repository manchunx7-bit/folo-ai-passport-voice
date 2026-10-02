#include "apps/radio/app_radio.h"
#include "apps/radio/radio_ui.h"
#include "apps/radio/radio_player.h"
#include "apps/radio/radio_catalog.h"
#include "wifi_manager.h"
#include "ssid_manager.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include <atomic>
#include <cstdlib>

static const char *TAG = "app_radio";

namespace {

std::atomic<bool> s_radio_active{false};
std::atomic<bool> s_wifi_connected{false};
std::atomic<bool> s_ui_alive{false};
// 2026-09-13 修复(D3):句柄改为原子。原来是普通 TaskHandle_t,由 worker 写
// NULL、由 start/on_wifi_event 读,多任务裸共享;快速进出收音机时可能通过
// "== nullptr" 判断而创建出第二个目录任务。
std::atomic<TaskHandle_t> s_catalog_task{nullptr};

void catalog_worker(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Starting IP-based location and station catalog search...");
    constexpr std::size_t kDiscoveryCapacity = 8;
    auto *stations = static_cast<RadioStation *>(
        std::calloc(kDiscoveryCapacity, sizeof(RadioStation)));
    if (!stations) {
        ESP_LOGE(TAG, "Unable to allocate station catalog");
        s_catalog_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    RadioLocation loc = {};
    std::size_t found = radio_catalog_discover(stations, kDiscoveryCapacity, &loc);
    // 2026-09-13 修复(C2):原来"检查 s_radio_active"与"动作"是两次独立读取,
    // 中间有窗口 —— 退出 App 后仍可能把播放拉起来,与下一个 App 抢 codec。
    // 现在只取一次快照,后续全部用快照判断。
    const bool alive = s_radio_active.load() && s_ui_alive.load();
    if (alive && found > 0) {
        ESP_LOGI(TAG, "Location detected: %s (%s), found %u stations", loc.city, loc.region, (unsigned)found);
        radio_ui_set_location(loc.city);
        radio_player_replace_stations_resuming(stations, found);
    }
    std::free(stations);
    if (alive) {
        // Catalog HTTP/cJSON can temporarily consume more than 24 KB. Start the
        // MP3 decoder only after those allocations are released.
        radio_player_set_playing(true);
    }
    s_catalog_task = nullptr;
    vTaskDelete(nullptr);
}

void on_wifi_event(WifiEvent event, const std::string &data) {
    if (!s_radio_active.load()) return;

    if (event == WifiEvent::Connected) {
        ESP_LOGI(TAG, "Wi-Fi connected: %s", data.c_str());
        s_wifi_connected.store(true);
        radio_ui_set_network(true, ("已连接: " + data).c_str());
        radio_player_set_network(true);

        if (s_catalog_task.load() == nullptr) {
            TaskHandle_t h = nullptr;
            if (xTaskCreate(catalog_worker, "radio_cat", 6144, nullptr, 5, &h) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create catalog task");
                radio_player_set_playing(true);
            } else {
                s_catalog_task.store(h);
            }
        }
    } else if (event == WifiEvent::Disconnected) {
        ESP_LOGI(TAG, "Wi-Fi disconnected: %s", data.c_str());
        s_wifi_connected.store(false);
        radio_ui_set_network(false, "Wi-Fi已断开");
        radio_player_set_network(false);
    }
}

} // namespace

void radio_app_init(void) {
    ESP_LOGI(TAG, "Initializing Radio App subsystems...");
    radio_ui_init();
    radio_player_init();
}

void radio_app_start(void) {
    ESP_LOGI(TAG, "Starting Radio App...");
    s_radio_active.store(true);
    s_ui_alive.store(true);

    // 1. 显示 Radio UI 界面
    radio_ui_show_main();
    radio_ui_set_volume(radio_player_volume());

    // 2. 挂接全局 Wi-Fi 事件回调
    WifiManager::GetInstance().SetEventCallback(on_wifi_event);
    if (!WifiManager::GetInstance().IsConnected()) {
        ESP_LOGI(TAG, "Wi-Fi not connected yet, checking station...");
        if (!SsidManager::GetInstance().GetSsidList().empty()) {
            radio_ui_set_network(false, "正在连接网络...");
            WifiManager::GetInstance().StartStation();
        } else {
            radio_ui_set_playback(RadioPlaybackState::Error, "请在手机设置中配网");
        }
    } else {
        radio_ui_set_network(true, "Wi-Fi已连接");
        radio_player_set_network(true);
        if (s_catalog_task.load() == nullptr) {
            TaskHandle_t h = nullptr;
            if (xTaskCreate(catalog_worker, "radio_cat", 6144, nullptr, 5, &h) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create catalog task");
                radio_player_set_playing(true);
            } else {
                s_catalog_task.store(h);
            }
        }
    }

    // 3. 目录查询释放 HTTP/cJSON 内存后再启动播放。
    radio_player_set_playing(false);
}

void radio_app_stop(void) {
    ESP_LOGI(TAG, "Stopping Radio App (keeping global Wi-Fi active)...");
    s_radio_active.store(false);

    // 1. 停止播放并等待拉流线程退出
    radio_player_set_playing(false);
    radio_player_wait_idle(1000);

    // 2. 2026-09-13 修复(C2):目录任务**必须 join**。原来只置标志就返回,
    //    任务还占着 6144B 栈 + 24KB 响应缓冲(单次 HTTP 最长 5s),紧接着进
    //    小智/语音时极易分配失败。这里等 5s 上界,超时也不 vTaskDelete
    //    (它可能正持堆锁),只记日志由它自行退出。
    s_ui_alive.store(false);
    int waited_ms = 0;
    while (s_catalog_task.load() != nullptr && waited_ms < 5000) {
        vTaskDelay(pdMS_TO_TICKS(50));
        waited_ms += 50;
    }
    if (s_catalog_task.load() != nullptr) {
        ESP_LOGW(TAG, "catalog task still running after 5s; left to exit by itself");
    }

    // 3. 移除事件回调，保留全局 Wi-Fi 连接
    WifiManager::GetInstance().SetEventCallback(nullptr);

    // 4. 隐藏 Radio UI 并暂停高频定时器
    radio_ui_hide();

    ESP_LOGI(TAG, "Radio App cleanly stopped. Free heap: %u KB",
             (unsigned)(esp_get_free_heap_size() / 1024));
}

void radio_app_on_key(uint8_t btn, app_btn_event_t event, uint16_t mv) {
    (void)mv;

    if (event == BTN_EVT_CLICK) {
        if (btn == BSP_BTN_UP) {
            // "+" 短按：切换下一电台
            radio_player_select_relative(1);
        } else if (btn == BSP_BTN_DOWN) {
            // "-" 短按：切换上一电台
            radio_player_select_relative(-1);
        } else if (btn == BSP_BTN_OK) {
            // "OK" 短按：播放 / 暂停切换
            radio_player_toggle();
        }
    } else if (event == BTN_EVT_LONG) {
        if (btn == BSP_BTN_UP) {
            // "+" 长按: 音量增加
            uint8_t cur = radio_player_volume();
            uint8_t next = cur >= 90 ? 100 : cur + 10;
            radio_player_set_volume(next);
            ESP_LOGI(TAG, "Radio volume up: %u -> %u", cur, next);
        } else if (btn == BSP_BTN_DOWN) {
            // "-" 长按: 音量减小
            uint8_t cur = radio_player_volume();
            uint8_t next = cur <= 10 ? 0 : cur - 10;
            radio_player_set_volume(next);
            ESP_LOGI(TAG, "Radio volume down: %u -> %u", cur, next);
        }
    }
}

const passport_app_t g_radio_app = {
    .id = APP_ID_RADIO,
    .name = "城市收音机",
    .en_name = "RADIO",
    .desc = "收听城市网络电台\n上下换台，确定播放",
    .tag = "Wi-Fi / FM",
    .theme_color = 0xFFB74D,
    .init = radio_app_init,
    .start = radio_app_start,
    .stop = radio_app_stop,
    .on_key = radio_app_on_key,
};
