#include "radio_easter_egg.h"

#include "radio_player.h"
#include "radio_ui.h"

#include "bsp_audio.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "simple_dec/esp_audio_simple_dec.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

constexpr char kTag[] = "radio_easter";
constexpr uint32_t kResourceAddress = 0x360000;
constexpr uint32_t kResourceEnd = 0x700000;
constexpr uint32_t kResourceCapacity = kResourceEnd - kResourceAddress;
constexpr uint8_t kMagic[8] = {'L', 'E', 'O', 'V', 'I', 'D', '1', '0'};
constexpr char kResourcePartitionLabel[] = "easter";
constexpr std::size_t kAudioInputSize = 1024;
constexpr std::size_t kAudioOutputSize = 8192;
// LVGL probes each JPEG header synchronously from this task. TJPGD's probe
// path alone needs more than the previous 4 KiB stack on ESP32-C3, causing a
// stack-canary fault as soon as the first frame is installed.
constexpr uint32_t kVideoTaskStackSize = 8192;

#pragma pack(push, 1)
struct ResourceHeader {
    uint8_t magic[8];
    uint16_t version;
    uint16_t header_size;
    uint16_t width;
    uint16_t height;
    uint32_t fps_milli;
    uint32_t duration_ms;
    uint32_t frame_count;
    uint32_t frame_index_offset;
    uint32_t frame_data_offset;
    uint32_t audio_offset;
    uint32_t audio_size;
    uint32_t total_size;
    uint32_t max_frame_size;
    uint32_t payload_crc32;
    uint32_t header_crc32;
    uint8_t reserved[4];
};

struct FrameEntry {
    uint32_t offset;
    uint32_t size;
};
#pragma pack(pop)

static_assert(sizeof(ResourceHeader) == 64, "Unexpected resource header size");
static_assert(sizeof(FrameEntry) == 8, "Unexpected frame index size");

std::atomic<bool> s_active{false};
std::atomic<bool> s_stop_requested{false};
std::atomic<bool> s_video_failed{false};
TaskHandle_t s_easter_task;
TaskHandle_t s_video_task;
ResourceHeader s_header;
int64_t s_started_at_us;
uint8_t *s_frame_buffers[2];
std::size_t s_frame_capacity;
const esp_partition_t *s_resource_partition;
uint32_t s_resource_capacity = kResourceCapacity;

bool flash_read(uint32_t relative_offset, void *target, std::size_t size) {
    if (!target || relative_offset > s_resource_capacity ||
        size > s_resource_capacity - relative_offset) {
        return false;
    }
    if (s_resource_partition) {
        return esp_partition_read(s_resource_partition, relative_offset, target,
                                  size) == ESP_OK;
    }
    return esp_flash_read(esp_flash_default_chip, target,
                          kResourceAddress + relative_offset, size) == ESP_OK;
}

bool resource_window_is_safe() {
    if (s_resource_partition) return true;
    uint32_t flash_size = 0;
    if (esp_flash_get_size(esp_flash_default_chip, &flash_size) != ESP_OK ||
        flash_size < kResourceEnd) {
        ESP_LOGW(kTag, "Resource window exceeds flash size");
        return false;
    }

    bool safe = true;
    esp_partition_iterator_t iterator =
        esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (iterator) {
        const esp_partition_t *partition = esp_partition_get(iterator);
        const uint32_t partition_end = partition->address + partition->size;
        if (partition->address < kResourceEnd && partition_end > kResourceAddress) {
            ESP_LOGW(kTag, "Resource window overlaps partition %s at 0x%lx",
                     partition->label, static_cast<unsigned long>(partition->address));
            safe = false;
            break;
        }
        iterator = esp_partition_next(iterator);
    }
    esp_partition_iterator_release(iterator);
    return safe;
}

bool resolve_resource_location() {
    s_resource_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        kResourcePartitionLabel);
    if (s_resource_partition) {
        s_resource_capacity = s_resource_partition->size;
        ESP_LOGI(kTag, "Using easter partition at 0x%lx (%lu bytes)",
                 static_cast<unsigned long>(s_resource_partition->address),
                 static_cast<unsigned long>(s_resource_capacity));
        return s_resource_capacity >= sizeof(ResourceHeader);
    }
    s_resource_partition = nullptr;
    s_resource_capacity = kResourceCapacity;
    return resource_window_is_safe();
}

bool validate_header(ResourceHeader *header) {
    if (!resolve_resource_location() ||
        !flash_read(0, header, sizeof(*header))) {
        return false;
    }
    if (std::memcmp(header->magic, kMagic, sizeof(kMagic)) != 0 ||
        header->version != 1 || header->header_size != sizeof(ResourceHeader) ||
        header->width == 0 || header->width > 240 ||
        header->height == 0 || header->height > 280 ||
        header->fps_milli < 1000 || header->fps_milli > 20000 ||
        header->duration_ms < 1000 || header->duration_ms > 10U * 60U * 1000U ||
        header->frame_count == 0 || header->frame_count > 4000 ||
        header->max_frame_size < 128 || header->max_frame_size > 32768 ||
        header->frame_index_offset != sizeof(ResourceHeader) ||
        header->frame_index_offset + header->frame_count * sizeof(FrameEntry) >
            header->frame_data_offset ||
        header->frame_data_offset >= header->audio_offset ||
        header->audio_size == 0 ||
        header->audio_offset + header->audio_size > header->total_size ||
        header->total_size > s_resource_capacity) {
        ESP_LOGW(kTag, "Invalid resource header");
        return false;
    }

    const uint32_t expected_crc = header->header_crc32;
    ResourceHeader copy = *header;
    copy.header_crc32 = 0;
    if (esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&copy),
                         sizeof(copy)) != expected_crc) {
        ESP_LOGW(kTag, "Resource header CRC mismatch");
        return false;
    }
    return true;
}

bool validate_payload(const ResourceHeader &header) {
    auto *buffer = static_cast<uint8_t *>(std::malloc(4096));
    if (!buffer) return false;
    uint32_t crc = 0;
    uint32_t offset = header.header_size;
    while (offset < header.total_size && !s_stop_requested.load()) {
        const uint32_t size =
            std::min<uint32_t>(4096, header.total_size - offset);
        if (!flash_read(offset, buffer, size)) {
            std::free(buffer);
            return false;
        }
        crc = esp_rom_crc32_le(crc, buffer, size);
        offset += size;
        vTaskDelay(1);
    }
    std::free(buffer);
    if (s_stop_requested.load()) return false;
    if (crc != header.payload_crc32) {
        ESP_LOGW(kTag, "Resource payload CRC mismatch: %08lx != %08lx",
                 static_cast<unsigned long>(crc),
                 static_cast<unsigned long>(header.payload_crc32));
        return false;
    }
    return true;
}

bool ensure_frame_buffers(std::size_t capacity) {
    if (s_frame_capacity >= capacity && s_frame_buffers[0] && s_frame_buffers[1]) {
        return true;
    }
    std::free(s_frame_buffers[0]);
    std::free(s_frame_buffers[1]);
    s_frame_buffers[0] = static_cast<uint8_t *>(std::malloc(capacity));
    s_frame_buffers[1] = static_cast<uint8_t *>(std::malloc(capacity));
    if (!s_frame_buffers[0] || !s_frame_buffers[1]) {
        std::free(s_frame_buffers[0]);
        std::free(s_frame_buffers[1]);
        s_frame_buffers[0] = nullptr;
        s_frame_buffers[1] = nullptr;
        s_frame_capacity = 0;
        return false;
    }
    s_frame_capacity = capacity;
    return true;
}

void release_frame_buffers() {
    std::free(s_frame_buffers[0]);
    std::free(s_frame_buffers[1]);
    s_frame_buffers[0] = nullptr;
    s_frame_buffers[1] = nullptr;
    s_frame_capacity = 0;
}

void video_task(void *) {
    uint32_t last_frame = UINT32_MAX;
    uint32_t displayed_frames = 0;
    std::size_t slot = 0;
    while (!s_stop_requested.load(std::memory_order_acquire)) {
        const int64_t elapsed_us = esp_timer_get_time() - s_started_at_us;
        if (elapsed_us < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const uint32_t elapsed_ms = static_cast<uint32_t>(elapsed_us / 1000);
        if (elapsed_ms >= s_header.duration_ms) break;
        const uint32_t frame = std::min<uint32_t>(
            static_cast<uint32_t>((static_cast<uint64_t>(elapsed_ms) *
                                   s_header.fps_milli) /
                                  1000000ULL),
            s_header.frame_count - 1);
        if (frame != last_frame) {
            FrameEntry entry = {};
            const uint32_t index_offset =
                s_header.frame_index_offset + frame * sizeof(FrameEntry);
            if (!flash_read(index_offset, &entry, sizeof(entry)) ||
                entry.size == 0 || entry.size > s_header.max_frame_size ||
                entry.offset > s_header.audio_offset - s_header.frame_data_offset ||
                entry.size > s_header.audio_offset - s_header.frame_data_offset -
                                 entry.offset) {
                s_video_failed.store(true);
                break;
            }
            slot ^= 1U;
            if (!flash_read(s_header.frame_data_offset + entry.offset,
                            s_frame_buffers[slot], entry.size) ||
                entry.size < 10 || s_frame_buffers[slot][0] != 0xff ||
                s_frame_buffers[slot][1] != 0xd8) {
                s_video_failed.store(true);
                break;
            }
            if (!radio_ui_show_easter_egg_frame(s_frame_buffers[slot], entry.size,
                                                 s_header.width,
                                                 s_header.height)) {
                s_video_failed.store(true);
                break;
            }
            ++displayed_frames;
            last_frame = frame;
        }
        vTaskDelay(1);
    }
    ESP_LOGI(kTag, "Displayed %lu of %lu video frames",
             static_cast<unsigned long>(displayed_frames),
             static_cast<unsigned long>(s_header.frame_count));
    s_stop_requested.store(true, std::memory_order_release);
    s_video_task = nullptr;
    if (s_easter_task) xTaskNotifyGive(s_easter_task);
    vTaskDelete(nullptr);
}

esp_audio_simple_dec_handle_t open_mp3_decoder() {
    esp_audio_simple_dec_cfg_t config = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
        .dec_cfg = nullptr,
        .cfg_size = 0,
        .use_frame_dec = false,
    };
    esp_audio_simple_dec_handle_t decoder = nullptr;
    return esp_audio_simple_dec_open(&config, &decoder) == ESP_AUDIO_ERR_OK
               ? decoder
               : nullptr;
}

void show_error_then_return(bool resume_radio) {
    radio_ui_show_easter_egg("彩蛋未安装");
    for (int i = 0; i < 15 && !s_stop_requested.load(); ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    radio_ui_finish_easter_egg();
    release_frame_buffers();
    if (resume_radio) radio_player_set_playing(true);
    s_active.store(false, std::memory_order_release);
    s_easter_task = nullptr;
    vTaskDelete(nullptr);
}

void easter_task(void *) {
    s_easter_task = xTaskGetCurrentTaskHandle();
    radio_ui_show_easter_egg("正在加载彩蛋");

    ResourceHeader header = {};
    if (!validate_header(&header)) {
        show_error_then_return(false);
        return;
    }

    const bool resume_radio = radio_player_is_playing();
    radio_player_set_playing(false);
    if (!radio_player_wait_idle(8000) || s_stop_requested.load() ||
        !validate_payload(header) ||
        !ensure_frame_buffers(header.max_frame_size)) {
        show_error_then_return(resume_radio);
        return;
    }
    s_header = header;

    auto *input = static_cast<uint8_t *>(std::malloc(kAudioInputSize));
    auto *output = static_cast<uint8_t *>(std::malloc(kAudioOutputSize));
    esp_audio_simple_dec_handle_t decoder = open_mp3_decoder();
    if (!input || !output || !decoder) {
        std::free(input);
        std::free(output);
        if (decoder) esp_audio_simple_dec_close(decoder);
        show_error_then_return(resume_radio);
        return;
    }

    bool format_ready = false;
    bool video_started = false;
    uint32_t audio_position = 0;
    while (audio_position < header.audio_size && !s_stop_requested.load()) {
        const uint32_t input_size = std::min<uint32_t>(
            kAudioInputSize, header.audio_size - audio_position);
        if (!flash_read(header.audio_offset + audio_position, input, input_size)) break;
        audio_position += input_size;
        esp_audio_simple_dec_raw_t raw = {
            .buffer = input,
            .len = input_size,
            .eos = audio_position == header.audio_size,
            .consumed = 0,
            .frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE,
        };
        while (raw.len > 0 && !s_stop_requested.load()) {
            esp_audio_simple_dec_out_t frame = {
                .buffer = output,
                .len = kAudioOutputSize,
                .needed_size = 0,
                .decoded_size = 0,
            };
            const uint32_t before = raw.len;
            const esp_audio_err_t result =
                esp_audio_simple_dec_process(decoder, &raw, &frame);
            if (result != ESP_AUDIO_ERR_OK) {
                ESP_LOGW(kTag, "MP3 decode failed: %d", result);
                raw.len = 0;
                break;
            }
            if (raw.consumed > raw.len) raw.consumed = raw.len;
            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;

            if (frame.decoded_size) {
                if (!format_ready) {
                    esp_audio_simple_dec_info_t info = {};
                    if (esp_audio_simple_dec_get_info(decoder, &info) !=
                            ESP_AUDIO_ERR_OK ||
                        info.bits_per_sample != 16 || info.channel != 1 ||
                        info.sample_rate < 8000 || info.sample_rate > 48000 ||
                        bsp_audio_set_format(info.sample_rate, 16, 1) != ESP_OK) {
                        s_video_failed.store(true);
                        s_stop_requested.store(true);
                        break;
                    }
                    // The shared driver already applies the system volume.
                    bsp_audio_set_volume(100);
                    format_ready = true;
                }
                if (!video_started) {
                    s_started_at_us = esp_timer_get_time();
                    if (xTaskCreate(video_task, "radio_video", kVideoTaskStackSize,
                                    nullptr, 5,
                                    &s_video_task) != pdPASS) {
                        s_video_task = nullptr;
                        s_video_failed.store(true);
                        s_stop_requested.store(true);
                        break;
                    }
                    video_started = true;
                }
                if (bsp_audio_write(frame.buffer, frame.decoded_size) != ESP_OK) {
                    s_video_failed.store(true);
                    s_stop_requested.store(true);
                    break;
                }
            }
            if (raw.len == before || raw.consumed == 0) break;
        }
    }

    esp_audio_simple_dec_close(decoder);
    std::free(output);
    std::free(input);
    s_stop_requested.store(true, std::memory_order_release);
    if (video_started && s_video_task) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500));
    }

    const bool failed = s_video_failed.load() || !format_ready || !video_started;
    if (failed) {
        radio_ui_show_easter_egg("彩蛋未安装");
        vTaskDelay(pdMS_TO_TICKS(1200));
    }
    radio_ui_finish_easter_egg();
    if (!s_video_task) {
        release_frame_buffers();
    } else {
        ESP_LOGW(kTag, "Video task did not stop; retaining frame buffers safely");
    }
    if (resume_radio) radio_player_set_playing(true);
    s_active.store(false, std::memory_order_release);
    s_easter_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

bool radio_easter_egg_start() {
    bool expected = false;
    if (!s_active.compare_exchange_strong(expected, true)) return false;
    s_stop_requested.store(false);
    s_video_failed.store(false);
    if (xTaskCreate(easter_task, "radio_easter", 7168, nullptr, 6,
                    &s_easter_task) != pdPASS) {
        s_easter_task = nullptr;
        s_active.store(false);
        return false;
    }
    return true;
}

void radio_easter_egg_stop() {
    s_stop_requested.store(true, std::memory_order_release);
}

bool radio_easter_egg_is_playing() {
    return s_active.load(std::memory_order_acquire);
}
