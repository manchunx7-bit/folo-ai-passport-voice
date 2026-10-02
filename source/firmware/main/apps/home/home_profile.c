#include "apps/home/home_profile.h"

#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "home_profile";

#define NVS_NAMESPACE "home_profile"
#define SPIFFS_BASE "/storage"
// 换名以弃用旧版 64×64 RGB565 头像(avatar.bin,8192B)—— 旧文件因尺寸校验
// 不再被识别,重新上传一次即可;文件留在 SPIFFS 里无害。
#define AVATAR_TMP_PATH SPIFFS_BASE "/portrait.tmp"
#define AVATAR_PATH SPIFFS_BASE "/portrait.bin"
#define SHARE_TMP_PATH SPIFFS_BASE "/share.tmp"
#define SHARE_PATH SPIFFS_BASE "/share.jpg"
#define SHARE_OLD_PATH SPIFFS_BASE "/share.bin"

// 资料读写在 httpd 任务与首页任务间并发,用临界区保护结构体与版本号。
// 单核 C3 上临界区会阻断其他任务,写路径(NVS/SPIFFS IO)必须放在临界区外。
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static home_profile_t s_profile;
static uint32_t s_version = 1;

static bool utf8_len_ok(const char *s, size_t cap) {
    return s && strnlen(s, cap) < cap; /* strlen 已含结尾 0 之外的全部字节 */
}

void home_profile_init(void) {
    memset(&s_profile, 0, sizeof(s_profile));
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGI(TAG, "No profile stored yet, using empty profile");
        return;
    }
    size_t len;
    len = sizeof(s_profile.name);
    nvs_get_str(handle, "name", s_profile.name, &len);
    len = sizeof(s_profile.signature);
    nvs_get_str(handle, "signature", s_profile.signature, &len);
    len = sizeof(s_profile.city);
    nvs_get_str(handle, "city", s_profile.city, &len);
    len = sizeof(s_profile.address);
    nvs_get_str(handle, "address", s_profile.address, &len);
    len = sizeof(s_profile.lat);
    nvs_get_str(handle, "lat", s_profile.lat, &len);
    len = sizeof(s_profile.lon);
    nvs_get_str(handle, "lon", s_profile.lon, &len);
    nvs_close(handle);
    ESP_LOGI(TAG, "Profile loaded: name='%s' city='%s' lat='%s' lon='%s'",
             s_profile.name, s_profile.city, s_profile.lat, s_profile.lon);
}

void home_profile_get(home_profile_t *out) {
    if (!out) return;
    portENTER_CRITICAL(&s_mux);
    *out = s_profile;
    portEXIT_CRITICAL(&s_mux);
}

uint32_t home_profile_version(void) {
    portENTER_CRITICAL(&s_mux);
    uint32_t v = s_version;
    portEXIT_CRITICAL(&s_mux);
    return v;
}

esp_err_t home_profile_set_text(const home_profile_t *in) {
    if (!in) return ESP_ERR_INVALID_ARG;
    if (!utf8_len_ok(in->name, HOME_NAME_MAX) ||
        !utf8_len_ok(in->signature, HOME_SIGNATURE_MAX) ||
        !utf8_len_ok(in->city, HOME_CITY_MAX) ||
        !utf8_len_ok(in->address, HOME_ADDRESS_MAX) ||
        !utf8_len_ok(in->lat, HOME_COORD_MAX) ||
        !utf8_len_ok(in->lon, HOME_COORD_MAX)) {
        return ESP_ERR_INVALID_SIZE;
    }

    // 先持久化再上屏:NVS 写失败时保持旧资料,UI 永远与存储一致。
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    do {
        if ((err = nvs_set_str(handle, "name", in->name)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "signature", in->signature)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "city", in->city)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "address", in->address)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "lat", in->lat)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "lon", in->lon)) != ESP_OK) break;
        err = nvs_commit(handle);
    } while (false);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Profile save failed: %s", esp_err_to_name(err));
        return err;
    }

    portENTER_CRITICAL(&s_mux);
    s_profile = *in;
    s_version++;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "Profile updated: name='%s' city='%s'", in->name, in->city);
    return ESP_OK;
}

static esp_err_t ensure_spiffs(void) {
    static bool mounted;
    if (mounted) return ESP_OK;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE,
        .partition_label = "storage",
        .max_files = 4,
        .format_if_mount_failed = false, // 掉分区宁可没有头像,不静默抹盘
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        // 空白新分区首次挂载可能失败,格式化一次再挂(仅此一次)
        ESP_LOGW(TAG, "SPIFFS mount failed (%s), formatting once", esp_err_to_name(err));
        conf.format_if_mount_failed = true;
        err = esp_vfs_spiffs_register(&conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS unavailable: %s", esp_err_to_name(err));
            return err;
        }
    }
    mounted = true;
    return ESP_OK;
}

bool home_profile_has_avatar(void) {
    if (ensure_spiffs() != ESP_OK) return false;
    struct stat st;
    return stat(AVATAR_PATH, &st) == 0 && st.st_size == HOME_AVATAR_BYTES;
}

esp_err_t home_profile_read_avatar(uint8_t *dst, size_t cap) {
    if (!dst || cap < HOME_AVATAR_BYTES) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ensure_spiffs();
    if (err != ESP_OK) return err;
    FILE *fp = fopen(AVATAR_PATH, "rb");
    if (!fp) return ESP_ERR_NOT_FOUND;
    size_t got = fread(dst, 1, HOME_AVATAR_BYTES, fp);
    fclose(fp);
    if (got != HOME_AVATAR_BYTES) {
        ESP_LOGW(TAG, "Avatar short read: %u/%u", (unsigned)got, HOME_AVATAR_BYTES);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t home_profile_write_avatar_chunk(const uint8_t *data, size_t len,
                                          bool first, bool last) {
    static FILE *s_stream;       // 仅 httpd 任务使用,无需加锁
    static size_t s_written;

    if (ensure_spiffs() != ESP_OK) return ESP_FAIL;
    if (first) {
        if (s_stream) {          // 上一次上传被打断:丢弃残留句柄
            fclose(s_stream);
            s_stream = NULL;
        }
        s_stream = fopen(AVATAR_TMP_PATH, "wb");
        s_written = 0;
        if (!s_stream) {
            ESP_LOGW(TAG, "Cannot open avatar tmp file");
            return ESP_FAIL;
        }
    }
    if (!s_stream) return ESP_ERR_INVALID_STATE;

    if (len) {
        if (fwrite(data, 1, len, s_stream) != len) {
            fclose(s_stream);
            s_stream = NULL;
            remove(AVATAR_TMP_PATH);
            return ESP_FAIL;
        }
        s_written += len;
    }

    if (!last) return ESP_OK;

    fclose(s_stream);
    s_stream = NULL;
    if (s_written != HOME_AVATAR_BYTES) {
        remove(AVATAR_TMP_PATH);
        ESP_LOGW(TAG, "Avatar upload wrong size %u", (unsigned)s_written);
        return ESP_ERR_INVALID_SIZE;
    }
    remove(AVATAR_PATH);
    if (rename(AVATAR_TMP_PATH, AVATAR_PATH) != 0) {
        ESP_LOGW(TAG, "Avatar rename failed");
        return ESP_FAIL;
    }
    portENTER_CRITICAL(&s_mux);
    s_version++;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "Avatar stored (%u bytes)", (unsigned)s_written);
    return ESP_OK;
}

const char *home_profile_share_image_path(void) {
    return SHARE_PATH;
}

bool home_profile_has_share_image(void) {
    if (ensure_spiffs() != ESP_OK) return false;
    struct stat st;
    return stat(SHARE_PATH, &st) == 0 && st.st_size > 0 && st.st_size <= HOME_SHARE_MAX_BYTES;
}

esp_err_t home_profile_write_share_image_chunk(const uint8_t *data, size_t len,
                                               bool first, bool last) {
    static FILE *s_stream;
    static size_t s_written;

    if (ensure_spiffs() != ESP_OK) return ESP_FAIL;
    if (first) {
        if (s_stream) {
            fclose(s_stream);
            s_stream = NULL;
        }
        s_stream = fopen(SHARE_TMP_PATH, "wb");
        s_written = 0;
        if (!s_stream) {
            ESP_LOGW(TAG, "Cannot open share image tmp file");
            return ESP_FAIL;
        }
    }
    if (!s_stream) return ESP_ERR_INVALID_STATE;

    if (len) {
        if (s_written + len > HOME_SHARE_MAX_BYTES) {
            fclose(s_stream);
            s_stream = NULL;
            remove(SHARE_TMP_PATH);
            ESP_LOGW(TAG, "Share image exceeded max size %u", (unsigned)HOME_SHARE_MAX_BYTES);
            return ESP_ERR_INVALID_SIZE;
        }
        if (fwrite(data, 1, len, s_stream) != len) {
            fclose(s_stream);
            s_stream = NULL;
            remove(SHARE_TMP_PATH);
            return ESP_FAIL;
        }
        s_written += len;
    }
    if (!last) return ESP_OK;

    fclose(s_stream);
    s_stream = NULL;
    if (s_written == 0 || s_written > HOME_SHARE_MAX_BYTES) {
        remove(SHARE_TMP_PATH);
        ESP_LOGW(TAG, "Share image upload invalid size %u", (unsigned)s_written);
        return ESP_ERR_INVALID_SIZE;
    }
    remove(SHARE_PATH);
    if (rename(SHARE_TMP_PATH, SHARE_PATH) != 0) {
        ESP_LOGW(TAG, "Share image rename failed");
        return ESP_FAIL;
    }
    remove(SHARE_OLD_PATH);
    portENTER_CRITICAL(&s_mux);
    s_version++;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "Share image stored (%u bytes JPEG)", (unsigned)s_written);
    return ESP_OK;
}
