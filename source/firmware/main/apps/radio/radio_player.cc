#include "radio_player.h"

#include "bsp_audio.h"
#include "launcher/settings.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "radio_ui.h"

#include "decoder/esp_audio_dec_default.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include "simple_dec/esp_audio_simple_dec_default.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

constexpr char kTag[] = "radio_player";
constexpr std::size_t kInputSize = 1024;
constexpr std::size_t kOutputSize = 8192;
constexpr std::size_t kLevelCount = 18;
constexpr std::size_t kMaxStations = 10;
// Consecutive silent failures tolerated before moving to the next station.
constexpr uint8_t kMaxFailedAttempts = 3;

// These national streams are not tied to one local transmitter, so they carry
// no dial frequency and the needle spreads them across the scale by position.
const RadioStation kFallbackStations[] = {
    {"中国之声", "新闻综合 · 网络直播",
     "http://lhttp.qingting.fm/live/15318317/64k.mp3", 0},
    {"怀旧音乐", "经典老歌 · 网络直播",
     "http://lhttp.qingting.fm/live/4804/64k.mp3", 0},
    {"清晨音乐", "轻松旋律 · 网络直播",
     "http://lhttp.qingting.fm/live/4915/64k.mp3", 0},
    {"两广之声", "粤语音乐 · 网络直播",
     "http://lhttp.qingting.fm/live/20500149/64k.mp3", 0},
    {"羊城交通", "城市资讯 · 网络直播",
     "http://lhttp.qingting.fm/live/1262/64k.mp3", 0},
    {"第一财经", "财经资讯 · 网络直播",
     "http://lhttp.qingting.fm/live/276/64k.mp3", 0},
};

std::atomic<bool> s_network_connected{false};
std::atomic<bool> s_wanted_playing{true};
std::atomic<std::size_t> s_station_index{0};
std::atomic<std::size_t> s_station_count{0};
std::atomic<uint32_t> s_generation{1};
std::atomic<bool> s_stream_active{false};
TaskHandle_t s_player_task;
SemaphoreHandle_t s_station_mutex;
RadioStation s_stations[kMaxStations] = {};

RadioStation station_snapshot(std::size_t index) {
    RadioStation station = {};
    if (!s_station_mutex ||
        xSemaphoreTake(s_station_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return station;
    }
    const std::size_t count = std::max<std::size_t>(s_station_count.load(), 1);
    station = s_stations[index % count];
    xSemaphoreGive(s_station_mutex);
    return station;
}

void save_setting(const char *key, uint8_t value) {
    nvs_handle_t handle;
    if (nvs_open("radio", NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_u8(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
}

uint8_t load_setting(const char *key, uint8_t fallback) {
    nvs_handle_t handle;
    if (nvs_open("radio", NVS_READONLY, &handle) != ESP_OK) return fallback;
    uint8_t value = fallback;
    if (nvs_get_u8(handle, key, &value) != ESP_OK) value = fallback;
    nvs_close(handle);
    return value;
}

void save_station_name(const char *name) {
    nvs_handle_t handle;
    if (nvs_open("radio", NVS_READWRITE, &handle) != ESP_OK) return;
    if (name && name[0]) {
        nvs_set_str(handle, "station_name", name);
    } else {
        nvs_erase_key(handle, "station_name");
    }
    nvs_commit(handle);
    nvs_close(handle);
}

// Copies the station name the listener last chose. Returns false when no
// station has been remembered yet.
bool load_station_name(char *name, std::size_t capacity) {
    if (!name || capacity == 0) return false;
    name[0] = '\0';
    nvs_handle_t handle;
    if (nvs_open("radio", NVS_READONLY, &handle) != ESP_OK) return false;
    std::size_t length = capacity;
    const esp_err_t error = nvs_get_str(handle, "station_name", name, &length);
    nvs_close(handle);
    if (error != ESP_OK) {
        name[0] = '\0';
        return false;
    }
    return name[0] != '\0';
}

// Installs a catalog under the station mutex and reports the index that should
// stay selected. `preferred_name` keeps the previous station when the fresh
// catalog still carries it.
bool install_stations(const RadioStation *stations, std::size_t count,
                      const char *preferred_name, std::size_t *selected) {
    if (!stations || count == 0 || !s_station_mutex) return false;
    count = std::min(count, kMaxStations);

    // Stop the current request before replacing its backing catalog. The
    // streaming task uses a value snapshot, so the swap is atomic to users.
    s_generation.fetch_add(1, std::memory_order_acq_rel);
    if (xSemaphoreTake(s_station_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    std::memcpy(s_stations, stations, count * sizeof(RadioStation));
    if (count < kMaxStations) {
        std::memset(s_stations + count, 0,
                    (kMaxStations - count) * sizeof(RadioStation));
    }
    std::size_t index = 0;
    if (preferred_name && preferred_name[0]) {
        for (std::size_t i = 0; i < count; ++i) {
            if (std::strcmp(s_stations[i].name, preferred_name) == 0) {
                index = i;
                break;
            }
        }
    }
    s_station_count.store(count, std::memory_order_release);
    s_station_index.store(index, std::memory_order_release);
    xSemaphoreGive(s_station_mutex);
    if (selected) *selected = index;
    return true;
}

bool request_still_current(uint32_t generation, std::size_t station) {
    return s_network_connected.load(std::memory_order_acquire) &&
           s_wanted_playing.load(std::memory_order_acquire) &&
           s_generation.load(std::memory_order_acquire) == generation &&
           s_station_index.load(std::memory_order_acquire) == station;
}

void calculate_levels(const uint8_t *pcm, std::size_t bytes, uint8_t channels) {
    if (!pcm || bytes < 64) return;
    const auto *samples = reinterpret_cast<const int16_t *>(pcm);
    const std::size_t sample_count = bytes / sizeof(int16_t);
    if (sample_count < kLevelCount) return;
    const std::size_t frames = sample_count / std::max<uint8_t>(channels, 1);
    if (frames < kLevelCount) return;

    uint8_t levels[kLevelCount] = {};
    for (std::size_t band = 0; band < kLevelCount; ++band) {
        const std::size_t begin = band * frames / kLevelCount;
        const std::size_t end = (band + 1) * frames / kLevelCount;
        int64_t energy = 0;
        std::size_t count = 0;
        for (std::size_t frame = begin; frame < end; ++frame) {
            int32_t mixed = 0;
            for (uint8_t channel = 0; channel < channels; ++channel) {
                mixed += samples[frame * channels + channel];
            }
            mixed /= std::max<uint8_t>(channels, 1);
            energy += static_cast<int64_t>(mixed) * mixed;
            ++count;
        }
        const float rms = count ? std::sqrt(static_cast<float>(energy) / count) : 0.0f;
        float db = rms > 1.0f ? 20.0f * std::log10(rms / 32768.0f) : -80.0f;
        int value = static_cast<int>((db + 58.0f) * 2.25f);
        value += static_cast<int>((band * 13 + s_generation.load()) % 9) - 4;
        levels[band] = static_cast<uint8_t>(std::clamp(value, 2, 100));
    }
    radio_ui_set_audio_levels(levels, kLevelCount);
}

std::size_t downmix_to_mono(uint8_t *pcm, std::size_t bytes, uint8_t channels) {
    if (!pcm || channels == 0) return 0;
    if (channels == 1) return bytes & ~static_cast<std::size_t>(1);

    auto *samples = reinterpret_cast<int16_t *>(pcm);
    const std::size_t frames = bytes / (sizeof(int16_t) * channels);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        int32_t mixed = 0;
        for (uint8_t channel = 0; channel < channels; ++channel) {
            mixed += samples[frame * channels + channel];
        }
        samples[frame] = static_cast<int16_t>(mixed / channels);
    }
    return frames * sizeof(int16_t);
}

esp_audio_simple_dec_handle_t open_mp3_decoder() {
    esp_audio_simple_dec_cfg_t config = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
        .dec_cfg = nullptr,
        .cfg_size = 0,
        .use_frame_dec = false,
    };
    esp_audio_simple_dec_handle_t decoder = nullptr;
    const esp_audio_err_t error = esp_audio_simple_dec_open(&config, &decoder);
    if (error != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(kTag, "MP3 decoder open failed: %d", error);
        return nullptr;
    }
    return decoder;
}

bool stream_station(std::size_t station, uint32_t generation) {
    const RadioStation preset = station_snapshot(station);
    if (!preset.url[0]) {
        ESP_LOGE(kTag, "Station %u has no stream URL", static_cast<unsigned>(station));
        return false;
    }
    radio_ui_set_playback(RadioPlaybackState::Connecting, "正在连接电台");

    esp_http_client_config_t config = {};
    config.url = preset.url;
    config.timeout_ms = 7000;
    config.buffer_size = 2048;
    config.buffer_size_tx = 512;
    config.user_agent = "AI-Passport-Radio/1.1.1";
    config.keep_alive_enable = true;
    config.disable_auto_redirect = false;
    config.max_redirection_count = 4;
    if (std::strncmp(preset.url, "https://", 8) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(kTag, "HTTP client allocation failed");
        return false;
    }
    esp_http_client_set_header(client, "Icy-MetaData", "0");
    esp_http_client_set_header(client, "Accept", "audio/mpeg,*/*");

    bool played_audio = false;
    esp_audio_simple_dec_handle_t decoder = nullptr;
    uint8_t *input = nullptr;
    uint8_t *output = nullptr;

    do {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            ESP_LOGW(kTag, "Open failed: %s", preset.url);
            break;
        }
        esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ESP_LOGW(kTag, "HTTP status %d", status);
            break;
        }

        decoder = open_mp3_decoder();
        if (!decoder) break;
        input = static_cast<uint8_t *>(malloc(kInputSize));
        output = static_cast<uint8_t *>(malloc(kOutputSize));
        if (!input || !output) {
            ESP_LOGE(kTag, "Not enough memory for stream buffers");
            break;
        }

        radio_ui_set_playback(RadioPlaybackState::Buffering, "正在缓冲");
        bool format_ready = false;
        uint8_t source_channels = 1;
        int empty_reads = 0;
        while (request_still_current(generation, station)) {
            const int received = esp_http_client_read(client,
                                                      reinterpret_cast<char *>(input),
                                                      kInputSize);
            if (received < 0) {
                ESP_LOGW(kTag, "Stream read error");
                break;
            }
            if (received == 0) {
                if (++empty_reads > 3) break;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            empty_reads = 0;

            esp_audio_simple_dec_raw_t raw = {
                .buffer = input,
                .len = static_cast<uint32_t>(received),
                .eos = false,
                .consumed = 0,
                .frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE,
            };
            while (raw.len > 0 && request_still_current(generation, station)) {
                esp_audio_simple_dec_out_t frame = {
                    .buffer = output,
                    .len = kOutputSize,
                    .needed_size = 0,
                    .decoded_size = 0,
                };
                const uint32_t before = raw.len;
                const esp_audio_err_t result =
                    esp_audio_simple_dec_process(decoder, &raw, &frame);
                if (result == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    ESP_LOGE(kTag, "Decoder needs %u bytes, buffer has %u",
                             static_cast<unsigned>(frame.needed_size),
                             static_cast<unsigned>(kOutputSize));
                    raw.len = 0;
                    break;
                }
                if (result != ESP_AUDIO_ERR_OK) {
                    ESP_LOGW(kTag, "MP3 decode failed: %d", result);
                    raw.len = 0;
                    break;
                }
                if (raw.consumed > raw.len) raw.consumed = raw.len;
                raw.buffer += raw.consumed;
                raw.len -= raw.consumed;

                if (frame.decoded_size > 0) {
                    esp_audio_simple_dec_info_t info = {};
                    if (!format_ready &&
                        esp_audio_simple_dec_get_info(decoder, &info) == ESP_AUDIO_ERR_OK) {
                        source_channels = std::clamp<uint8_t>(info.channel, 1, 2);
                        if (info.bits_per_sample != 16 || info.sample_rate < 8000 ||
                            info.sample_rate > 48000 ||
                            bsp_audio_set_format(info.sample_rate, 16, 1) != ESP_OK) {
                            ESP_LOGE(kTag, "Unsupported stream format: %luHz/%ubit/%uch",
                                     static_cast<unsigned long>(info.sample_rate),
                                     info.bits_per_sample, source_channels);
                            raw.len = 0;
                            break;
                        }
                        // Radio and Xiaozhi share one user-visible master volume.
                        // Do not multiply it by the legacy radio NVS volume again.
                        bsp_audio_set_volume(100);
                        ESP_LOGI(kTag, "Radio output: system=%u%%, app=100%%",
                                 static_cast<unsigned>(bsp_audio_get_master_volume()));
                        format_ready = true;
                        ESP_LOGI(kTag, "Playing %s at %luHz/%uch -> mono", preset.name,
                                 static_cast<unsigned long>(info.sample_rate), source_channels);
                    }
                    if (format_ready) {
                        const std::size_t mono_bytes =
                            downmix_to_mono(frame.buffer, frame.decoded_size, source_channels);
                        if (bsp_audio_write(frame.buffer, mono_bytes) != ESP_OK) continue;
                        calculate_levels(frame.buffer, mono_bytes, 1);
                        if (!played_audio) {
                            played_audio = true;
                            radio_ui_set_playback(RadioPlaybackState::Playing, "正在播放");
                        }
                    }
                }

                if (raw.len == before || raw.consumed == 0) {
                    // The parser may cache a short incomplete tail. A fresh network
                    // block is required; keeping this block would spin forever.
                    break;
                }
            }
        }
    } while (false);

    if (decoder) esp_audio_simple_dec_close(decoder);
    free(output);
    free(input);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return played_audio;
}

void player_task(void *) {
    std::size_t last_attempted_station = SIZE_MAX;
    uint8_t failed_attempts = 0;
    bool reported_no_network = false;
    bool reported_paused = false;

    while (true) {
        if (!s_network_connected.load(std::memory_order_acquire)) {
            if (!reported_no_network) {
                radio_ui_set_playback(RadioPlaybackState::Stopped, "等待网络");
                reported_no_network = true;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        reported_no_network = false;

        if (!s_wanted_playing.load(std::memory_order_acquire)) {
            if (!reported_paused) {
                radio_ui_set_playback(RadioPlaybackState::Stopped, "已暂停");
                reported_paused = true;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        reported_paused = false;

        const std::size_t station = s_station_index.load(std::memory_order_acquire);
        const uint32_t generation = s_generation.load(std::memory_order_acquire);
        if (station != last_attempted_station) {
            last_attempted_station = station;
            failed_attempts = 0;
        }
        s_stream_active.store(true, std::memory_order_release);
        const bool played = stream_station(station, generation);
        s_stream_active.store(false, std::memory_order_release);
        if (!request_still_current(generation, station)) continue;

        if (played) {
            // The stream worked at least briefly, so this station is alive and
            // simply dropped. Keep it and reconnect.
            failed_attempts = 0;
            radio_ui_set_playback(RadioPlaybackState::Reconnecting,
                                  "信号中断，正在重连");
            vTaskDelay(pdMS_TO_TICKS(1200));
            continue;
        }

        ++failed_attempts;
        const std::size_t count = radio_player_station_count();
        // Public directories keep dead entries around. After a few silent
        // failures, move on instead of retrying the same address forever.
        if (failed_attempts >= kMaxFailedAttempts && count > 1) {
            ESP_LOGW(kTag, "Station %u unreachable, skipping to the next one",
                     static_cast<unsigned>(station));
            radio_ui_set_playback(RadioPlaybackState::Error,
                                  "电台无法播放 已换台");
            vTaskDelay(pdMS_TO_TICKS(1200));
            failed_attempts = 0;
            radio_player_select_relative(1);
            continue;
        }

        radio_ui_set_playback(RadioPlaybackState::Error, "连接失败，正在重试");
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}

}  // namespace

bool radio_player_init() {
    if (s_player_task != nullptr) {
        return true;
    }
    s_station_mutex = xSemaphoreCreateMutex();
    if (!s_station_mutex) return false;
    std::memcpy(s_stations, kFallbackStations, sizeof(kFallbackStations));
    s_station_count.store(sizeof(kFallbackStations) / sizeof(kFallbackStations[0]));

    const uint8_t saved_station = load_setting("station", 0);
    s_station_index.store(saved_station % radio_player_station_count());

    if (bsp_audio_init() != ESP_OK) return false;
    if (esp_audio_dec_register_default() != ESP_AUDIO_ERR_OK) return false;
    if (esp_audio_simple_dec_register_default() != ESP_AUDIO_ERR_OK) return false;
    bsp_audio_set_volume(100);

    radio_ui_set_station(s_station_index.load(), radio_player_station_count(),
                         radio_player_station(s_station_index.load()));
    radio_ui_set_volume(radio_player_volume());
    if (xTaskCreate(player_task, "radio_stream", 7168, nullptr, 6, &s_player_task) != pdPASS) {
        s_player_task = nullptr;
        return false;
    }
    return true;
}

void radio_player_set_network(bool connected) {
    s_network_connected.store(connected, std::memory_order_release);
    s_generation.fetch_add(1, std::memory_order_acq_rel);
}

void radio_player_toggle() {
    radio_player_set_playing(!radio_player_is_playing());
}

void radio_player_set_playing(bool playing) {
    s_wanted_playing.store(playing, std::memory_order_release);
    s_generation.fetch_add(1, std::memory_order_acq_rel);
    if (!playing) radio_ui_set_playback(RadioPlaybackState::Stopped, "已暂停");
}

bool radio_player_is_playing() {
    return s_wanted_playing.load(std::memory_order_acquire);
}

bool radio_player_wait_idle(uint32_t timeout_ms) {
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (s_stream_active.load(std::memory_order_acquire)) {
        if (static_cast<int32_t>(deadline - xTaskGetTickCount()) <= 0) return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

void radio_player_select_relative(int delta) {
    const int count = static_cast<int>(radio_player_station_count());
    const int current = static_cast<int>(s_station_index.load());
    const std::size_t next = static_cast<std::size_t>((current + delta + count) % count);
    radio_player_set_station(next);
}

void radio_player_set_station(std::size_t index) {
    index %= radio_player_station_count();
    s_station_index.store(index, std::memory_order_release);
    s_generation.fetch_add(1, std::memory_order_acq_rel);
    save_setting("station", static_cast<uint8_t>(index));
    const RadioStation chosen = radio_player_station(index);
    save_station_name(chosen.name);
    radio_ui_set_station(index, radio_player_station_count(), radio_player_station(index));
    if (radio_player_is_playing()) {
        radio_ui_set_playback(RadioPlaybackState::Connecting, "正在切换电台");
    }
}

std::size_t radio_player_station_index() {
    return s_station_index.load(std::memory_order_acquire);
}

std::size_t radio_player_station_count() {
    return std::max<std::size_t>(s_station_count.load(std::memory_order_acquire), 1);
}

RadioStation radio_player_station(std::size_t index) {
    return station_snapshot(index);
}

bool radio_player_replace_stations(const RadioStation *stations, std::size_t count) {
    std::size_t selected = 0;
    if (!install_stations(stations, count, nullptr, &selected)) return false;

    save_setting("station", static_cast<uint8_t>(selected));
    const RadioStation chosen = radio_player_station(selected);
    save_station_name(chosen.name);
    radio_ui_set_station(selected, radio_player_station_count(), chosen);
    ESP_LOGI(kTag, "Installed local catalog with %u stations",
             static_cast<unsigned>(radio_player_station_count()));
    return true;
}

bool radio_player_replace_stations_resuming(const RadioStation *stations,
                                           std::size_t count) {
    char remembered[sizeof(RadioStation::name)] = {};
    const bool has_remembered = load_station_name(remembered, sizeof(remembered));

    std::size_t selected = 0;
    if (!install_stations(stations, count, has_remembered ? remembered : nullptr,
                          &selected)) {
        return false;
    }

    save_setting("station", static_cast<uint8_t>(selected));
    const RadioStation chosen = radio_player_station(selected);
    save_station_name(chosen.name);
    radio_ui_set_station(selected, radio_player_station_count(), chosen);
    if (has_remembered && selected > 0) {
        ESP_LOGI(kTag, "Resumed remembered station '%s' at index %u",
                 chosen.name, static_cast<unsigned>(selected));
    } else {
        ESP_LOGI(kTag, "Installed local catalog with %u stations",
                 static_cast<unsigned>(radio_player_station_count()));
    }
    return true;
}

void radio_player_set_volume(uint8_t volume) {
    settings_set_volume(volume);
    radio_ui_set_volume(radio_player_volume());
}

uint8_t radio_player_volume() {
    return bsp_audio_get_master_volume();
}
