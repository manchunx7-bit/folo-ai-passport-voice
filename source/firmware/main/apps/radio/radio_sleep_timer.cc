#include "radio_sleep_timer.h"

#include "radio_player.h"

#include "bsp_display.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <atomic>

namespace {

constexpr char kTag[] = "sleep_timer";
constexpr uint16_t kChoices[] = {0, 15, 30, 60, 90};

esp_timer_handle_t s_timer;
TaskHandle_t s_shutdown_task;
std::atomic<std::size_t> s_selection{0};
std::atomic<TickType_t> s_deadline_ticks{0};

void timer_callback(void *) {
    s_deadline_ticks.store(0, std::memory_order_release);
    if (s_shutdown_task) xTaskNotifyGive(s_shutdown_task);
}

void shutdown_task(void *) {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(kTag, "Sleep timer expired; shutting down");

        // End the stream first, then release the network before blanking the
        // display. The short delay lets the player task close its HTTP client.
        radio_player_set_playing(false);
        radio_player_set_network(false);
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(200));
        bsp_display_backlight(0);
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_deep_sleep_start();
    }
}

}  // namespace

bool radio_sleep_timer_init() {
    if (s_timer && s_shutdown_task) return true;
    if (!s_shutdown_task &&
        xTaskCreate(shutdown_task, "radio_shutdown", 2048, nullptr, 7,
                    &s_shutdown_task) != pdPASS) {
        s_shutdown_task = nullptr;
        return false;
    }

    const esp_timer_create_args_t config = {
        .callback = timer_callback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "radio_sleep",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&config, &s_timer) != ESP_OK) {
        vTaskDelete(s_shutdown_task);
        s_shutdown_task = nullptr;
        s_timer = nullptr;
        return false;
    }
    return true;
}

void radio_sleep_timer_set_minutes(uint16_t minutes) {
    std::size_t selection = 0;
    for (std::size_t i = 0; i < sizeof(kChoices) / sizeof(kChoices[0]); ++i) {
        if (kChoices[i] == minutes) {
            selection = i;
            break;
        }
    }
    s_selection.store(selection, std::memory_order_release);
    s_deadline_ticks.store(0, std::memory_order_release);

    if (!s_timer && !radio_sleep_timer_init()) {
        ESP_LOGE(kTag, "Unable to initialize timer");
        return;
    }
    esp_timer_stop(s_timer);
    if (minutes == 0) {
        ESP_LOGI(kTag, "Sleep timer cancelled");
        return;
    }

    const uint64_t delay_us = static_cast<uint64_t>(minutes) * 60ULL * 1000000ULL;
    const esp_err_t error = esp_timer_start_once(s_timer, delay_us);
    if (error != ESP_OK) {
        s_selection.store(0, std::memory_order_release);
        ESP_LOGE(kTag, "Unable to start %u-minute timer: %s",
                 static_cast<unsigned>(minutes), esp_err_to_name(error));
        return;
    }
    s_deadline_ticks.store(
        xTaskGetTickCount() +
            pdMS_TO_TICKS(static_cast<uint32_t>(minutes) * 60U * 1000U),
        std::memory_order_release);
    ESP_LOGI(kTag, "Sleep timer set to %u minutes",
             static_cast<unsigned>(minutes));
}

std::size_t radio_sleep_timer_selection() {
    return s_selection.load(std::memory_order_acquire);
}

uint32_t radio_sleep_timer_remaining_seconds() {
    if (radio_sleep_timer_selection() == 0) return 0;
    const TickType_t deadline = s_deadline_ticks.load(std::memory_order_acquire);
    if (!deadline) return 0;
    const int32_t remaining_ticks =
        static_cast<int32_t>(deadline - xTaskGetTickCount());
    if (remaining_ticks <= 0) return 0;
    return (static_cast<uint32_t>(remaining_ticks) + configTICK_RATE_HZ - 1U) /
           configTICK_RATE_HZ;
}
