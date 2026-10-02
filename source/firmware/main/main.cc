#include "launcher/launcher.h"
#include "launcher/app_registry.h"
#include "boot_chime.h"
#include "boot_splash.h"
#include "launcher/settings.h"
#include "launcher/wifi_setup.h"
#include "wifi_manager.h"
#include "ssid_manager.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "driver/gpio.h"
#include "bsp_i2c.h"
#include "ui/ui_capture.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "main";

struct KeyMessage {
    uint8_t button;
    app_btn_event_t event;
    uint16_t mv;
};

static QueueHandle_t s_key_queue = nullptr;

// 2026-09-13 修复(D5):原来 11 处 xQueueSend 全都不看返回值。队列满时
// (消费端 input_task 卡在 LVGL 锁或应用切换上)按键被静默丢弃,表现为
// "按了没反应"却查不到任何线索。集中到这一个函数:失败时限频告警,
// 一眼能看出是按键侧丢还是消费端卡住。
static void key_post(const KeyMessage &msg) {
    if (xQueueSend(s_key_queue, &msg, 0) == pdTRUE) return;
    static TickType_t s_last_warn = 0;
    const TickType_t now = xTaskGetTickCount();
    if (now - s_last_warn >= pdMS_TO_TICKS(2000)) {
        s_last_warn = now;
        ESP_LOGW(TAG, "key queue full: dropped btn=%u evt=%u", msg.button, (unsigned)msg.event);
    }
}

// 串口调试命令走同一条队列;这里允许短暂等待,避免手敲命令被瞬间突发挤掉。
static void key_post_wait(const KeyMessage &msg) {
    if (xQueueSend(s_key_queue, &msg, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW(TAG, "key queue busy: dropped btn=%u evt=%u", msg.button, (unsigned)msg.event);
    }
}

static int button_from_mv(int millivolts) {
    if (millivolts < 0 || millivolts >= 2500) return -1;
    if (millivolts < 400) return BSP_BTN_UP;
    if (millivolts < 1600) return BSP_BTN_DOWN;
    return BSP_BTN_OK;
}

static void button_task(void *arg) {
    (void)arg;
    int candidate = -1;
    int stable = -1;
    uint8_t samples = 0;
    TickType_t pressed_at = 0;
    bool long_sent = false;
    uint16_t last_mv = 0;

    while (true) {
        int mv = bsp_button_read_mv();
        const int current = button_from_mv(mv);
        if (current >= 0) {
            last_mv = static_cast<uint16_t>(mv);
        }

        if (current == candidate) {
            if (samples < 3) ++samples;
        } else {
            candidate = current;
            samples = 1;
        }

        if (samples >= 3 && candidate != stable) {
            const int previous = stable;
            stable = candidate;
            ESP_LOGI(TAG, "Key Transition: stable=%d (prev=%d), mv=%u", stable, previous, last_mv);
            if (stable >= 0) {
                // 按下瞬间 (PRESS)
                pressed_at = xTaskGetTickCount();
                long_sent = false;
                KeyMessage msg = {
                    .button = static_cast<uint8_t>(stable),
                    .event = BTN_EVT_PRESS,
                    .mv = last_mv
                };
                key_post(msg);
            } else if (previous >= 0) {
                // 松开瞬间
                if (!long_sent) {
                    KeyMessage msg_click = {
                        .button = static_cast<uint8_t>(previous),
                        .event = BTN_EVT_CLICK,
                        .mv = last_mv
                    };
                    key_post(msg_click);
                } else {
                    KeyMessage msg_up = {
                        .button = static_cast<uint8_t>(previous),
                        .event = BTN_EVT_LONG_UP,
                        .mv = last_mv
                    };
                    key_post(msg_up);
                }
                KeyMessage msg_rel = {
                    .button = static_cast<uint8_t>(previous),
                    .event = BTN_EVT_RELEASE,
                    .mv = last_mv
                };
                key_post(msg_rel);
            }
        }

        // 长按判定 (800ms)
        if (stable >= 0 && !long_sent &&
            (xTaskGetTickCount() - pressed_at >= pdMS_TO_TICKS(800))) {
            KeyMessage msg_long = {
                .button = static_cast<uint8_t>(stable),
                .event = BTN_EVT_LONG,
                .mv = last_mv
            };
            key_post(msg_long);
            long_sent = true;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void input_task(void *arg) {
    (void)arg;
    KeyMessage msg;
    while (true) {
        if (xQueueReceive(s_key_queue, &msg, portMAX_DELAY) == pdTRUE) {
            app_registry_dispatch_key(msg.button, msg.event, msg.mv);
        }
    }
}

static void system_monitor_task(void *arg) {
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        wifi_setup_tick();
        app_registry_tick();
        if (app_registry_get_active() == APP_ID_LAUNCHER) {
            bsp_lvgl_lock(-1);
            launcher_ui_update_status();
            bsp_lvgl_unlock();
        }
    }
}

static void serial_cmd_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Serial command listener started ('u'=Up, 'd'=Down, 'o'=OK, 'l'=Long-OK, 'k'=Long-Down, '1'-'9'=App)");
    while (true) {
        int c = fgetc(stdin);
        if (c != EOF && c > 0) {
            if (c == 'u' || c == 'U' || c == '+') {
                ESP_LOGI(TAG, "[SerialCmd] UP (+) button clicked");
                KeyMessage msg = {.button = BSP_BTN_UP, .event = BTN_EVT_CLICK, .mv = 15};
                key_post_wait(msg);
            } else if (c == 'd' || c == 'D' || c == '-') {
                ESP_LOGI(TAG, "[SerialCmd] DOWN (-) button clicked");
                KeyMessage msg = {.button = BSP_BTN_DOWN, .event = BTN_EVT_CLICK, .mv = 1060};
                key_post_wait(msg);
            } else if (c == 'o' || c == 'O' || c == '\r' || c == '\n') {
                ESP_LOGI(TAG, "[SerialCmd] OK button clicked");
                KeyMessage msg = {.button = BSP_BTN_OK, .event = BTN_EVT_CLICK, .mv = 2070};
                key_post_wait(msg);
            } else if (c == 'l' || c == 'L' || c == 'b' || c == 'B') {
                ESP_LOGI(TAG, "[SerialCmd] LONG OK / Back button triggered");
                KeyMessage msg = {.button = BSP_BTN_OK, .event = BTN_EVT_LONG, .mv = 2070};
                key_post_wait(msg);
            } else if (c == 'k' || c == 'K') {
                // 调试用:长按下键。首页的上传模式(手机配网/传头像)绑在这个手势上,
                // 没有串口映射就无法脱离真机按键验证。
                ESP_LOGI(TAG, "[SerialCmd] LONG DOWN (-) button triggered");
                KeyMessage msg = {.button = BSP_BTN_DOWN, .event = BTN_EVT_LONG, .mv = 1060};
                key_post_wait(msg);
            } else if (c == '[' || c == ']') {
                // Diagnostic PTT pair: same event queue and handler as the physical UP key.
                ESP_LOGI(TAG, "[SerialCmd] UP %s", c == '[' ? "press" : "release");
                KeyMessage msg = {.button = BSP_BTN_UP,
                    .event = c == '[' ? BTN_EVT_PRESS : BTN_EVT_RELEASE,
                    .mv = static_cast<uint16_t>(c == '[' ? 15 : 2900)};
                key_post_wait(msg);
            } else if (c == 'p' || c == 'P') {
                // 真机 UI 截图：按横带渲染当前活动屏，把 RGB565 像素以十六进制从
                // console 吐出（主机端 _diag/capture_ui.py 收流并还原成 PNG）。
                // 必须持 LVGL 锁调用（lv_obj_redraw 要在 LVGL 上下文里跑），
                // 整个过程 ~1s，期间其它任务的 LVGL 操作会排队。
                ESP_LOGI(TAG, "[SerialCmd] screenshot requested");
                if (bsp_lvgl_lock(pdMS_TO_TICKS(2000))) {
                    ui_capture_dump_hex();
                    bsp_lvgl_unlock();
                } else {
                    ESP_LOGW(TAG, "[SerialCmd] screenshot aborted: LVGL lock busy");
                }
            } else if (c == 'j') {
                app_registry_switch_to(APP_ID_SOUND_JUMP);
            } else if (c >= '1' && c <= '9') {
                int target_id = c - '0';
                ESP_LOGI(TAG, "[SerialCmd] Direct switch to App ID %d", target_id);
                app_registry_switch_to(static_cast<passport_app_id_t>(target_id));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

extern "C" void app_main(void) {    // 注:上电瞬间的白屏由 bootloader_components/bl_early 在二级 bootloader 里
    // 就把 GPIO21 拉低解决(已实测 out=0/oe=1 一路保持到 app_main)。
    // 详见该目录下的注释。
    // 最早时刻把背光脚拉成确定低电平:上电到 LEDC 初始化之间该脚悬空,
    // 背光电路会被"点着",而面板此时是未写入的白屏 —— 这就是开机白闪。
    // (第二级 bootloader 阶段仍无法覆盖,那一小段由硬件上拉/下拉决定。)
    {
        gpio_config_t bl = {
            .pin_bit_mask = 1ULL << BSP_LCD_BL,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&bl);
        gpio_set_level((gpio_num_t)BSP_LCD_BL, 0);
    }

    ESP_LOGI(TAG, "=== FoloToy AI Passport OS Starting ===");

    // 1. NVS 初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 硬件总线与外设初始化
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_display_init());
    if (bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "LVGL initialization failed");
        return;
    }
    settings_bootstrap();
    // 背光不在这里点亮:此时面板 GRAM 尚未写入任何帧,亮背光会闪白屏。
    // 背光由 boot_splash 在首帧渲染完成后渐亮(见 boot_splash.c)。

    bsp_battery_init();
    // 仅初始化 ADC 采样硬件，不启动有死锁风险的 timer 回调
    bsp_button_init(nullptr, nullptr);

    // 3. 初始化 Launcher 与应用注册表 (仅载入启动器，不预加载子 App)
    app_registry_init();
    // 磷光开机动画(~0.9s,与开机音同步);不再提前初始化/加载 Launcher ——
    // 旧路径会先闪一两帧应用页再跳首页。抽屉首次进入时才会自建界面。
    boot_splash_run();


    // 4. 初始化全局 Wi-Fi 管理器；若已存有配置则在后台自动拉起 Station 连接
    WifiManagerConfig wifi_cfg = {};
    wifi_cfg.ssid_prefix = "Passport-WiFi";
    wifi_cfg.language = "zh-CN";
    WifiManager::GetInstance().Initialize(wifi_cfg);

    // 待机省电默认档:MAX modem sleep(首页/桌面/设置等非流媒体前台)。
    // 进 radio/voice/xiaozhi 时由 app_registry 切回 BALANCED,上传模式由首页恢复。
    WifiManager::GetInstance().SetPowerSaveLevel(WifiPowerSaveLevel::LOW_POWER);

    if (!SsidManager::GetInstance().GetSsidList().empty()) {
        ESP_LOGI(TAG, "Stored Wi-Fi found, starting station auto-connect in background...");
        WifiManager::GetInstance().StartStation();
        app_registry_switch_to(APP_ID_HOME);
    } else {
        ESP_LOGI(TAG, "First boot: starting phone setup");
        wifi_setup_require_initial();
        app_registry_switch_to(APP_ID_WIFI);
    }

    boot_splash_end();

    // 5. 创建按键事件队列与输入分发任务 (系统就绪后正式接收用户输入)
    //    input_disp/ser_cmd 栈给 8192:两者都会执行 app_registry_switch_to(),
    //    应用启动里最深的是 Home 天气图标 JPEG 解码 —— LVGL TJPEG
    //    decoder_info(文件源)在栈上放 4KB 工作缓冲(lv_tjpgd.c:110),
    //    3584/4096 栈实测爆栈(Stack protection fault,2026-09-13)。
    s_key_queue = xQueueCreate(16, sizeof(KeyMessage));
    xTaskCreate(button_task, "btn_poll", 2048, NULL, 6, NULL);
    xTaskCreate(input_task, "input_disp", 8192, NULL, 5, NULL);
    xTaskCreate(serial_cmd_task, "ser_cmd", 8192, NULL, 4, NULL);

    // 6. 启动系统监控心跳任务 (状态栏与统一息屏管理)
    xTaskCreate(system_monitor_task, "sys_mon", 2560, NULL, 1, NULL);

    // 复古开机音(异步短命任务,播完自动归还音频内存)
    boot_chime_start();

    ESP_LOGI(TAG, "Passport OS Boot Complete. Initial Free Heap: %u KB",
             (unsigned)(esp_get_free_heap_size() / 1024));
}
