#include "apps/home/home_weather.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "home_weather";

#define NVS_NAMESPACE "home_profile"
#define RESPONSE_MAX 1024 /* forecast?current= 响应 < 500B,留余量 */
#define HTTP_TIMEOUT_MS 8000

static home_weather_t s_weather;
static uint32_t s_version;

void home_weather_init(void) {
    memset(&s_weather, 0, sizeof(s_weather));
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        int16_t temp = 0;
        int16_t code = 0;
        if (nvs_get_i16(handle, "wtemp", &temp) == ESP_OK &&
            nvs_get_i16(handle, "wcode", &code) == ESP_OK) {
            s_weather.valid = true;
            s_weather.temp_c = temp;
            s_weather.code = (uint16_t)code;
            uint8_t u8 = 0;
            if (nvs_get_u8(handle, "whum", &u8) == ESP_OK) s_weather.humidity = u8;
            if (nvs_get_u8(handle, "wwind", &u8) == ESP_OK) s_weather.wind_kmh = u8;
            uint16_t dir = 0;
            if (nvs_get_u16(handle, "wdir", &dir) == ESP_OK) s_weather.wind_dir = dir;
        }
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Weather cache: valid=%d temp=%d code=%u",
             (int)s_weather.valid, (int)s_weather.temp_c, (unsigned)s_weather.code);
}

bool home_weather_get(home_weather_t *out) {
    if (!out) return false;
    *out = s_weather;
    return s_weather.valid;
}

uint32_t home_weather_version(void) { return s_version; }

const char *home_weather_code_text(uint16_t code) {
    // 短语只由 buddy 字体已有的字组成(雨雪雾雷冰雹晴阴多云大小中等均已验证)
    if (code == 0 || code == 1) return "晴";
    if (code == 2) return "多云";
    if (code == 3) return "阴";
    if (code == 45 || code == 48) return "雾";
    if (code >= 51 && code <= 55) return "毛毛雨";
    if (code == 56 || code == 57) return "冻雨";
    if (code == 61) return "小雨";
    if (code == 63) return "中雨";
    if (code == 65) return "大雨";
    if (code == 66 || code == 67) return "冻雨";
    if (code == 71 || code == 77) return "小雪";
    if (code == 73) return "中雪";
    if (code == 75) return "大雪";
    if (code == 80 || code == 81) return "阵雨";
    if (code == 82) return "强阵雨";
    if (code == 85 || code == 86) return "阵雪";
    if (code == 95) return "雷雨";
    if (code == 96 || code == 99) return "雷雨冰雹";
    return "未知";
}

static void persist_cache(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_i16(handle, "wtemp", s_weather.temp_c) == ESP_OK &&
        nvs_set_i16(handle, "wcode", (int16_t)s_weather.code) == ESP_OK &&
        nvs_set_u8(handle, "whum", s_weather.humidity) == ESP_OK &&
        nvs_set_u8(handle, "wwind", s_weather.wind_kmh) == ESP_OK &&
        nvs_set_u16(handle, "wdir", s_weather.wind_dir) == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

// 蒲福风级:km/h → 0..12 级,阈值来自国标蒲福风级表。
int home_weather_beaufort(uint8_t wind_kmh) {
    static const uint8_t kThresh[] = {1, 6, 12, 20, 29, 39, 50, 62, 75, 89, 103, 118};
    int level = 0;
    while (level < 12 && wind_kmh >= kThresh[level]) ++level;
    return level;
}

// 风向 16 点位就近归到 8 方位(北=0°,顺时针)。字形 东南西北风无 均在 buddy 字库内。
const char *home_weather_wind_dir_text(uint16_t deg) {
    static const char *kDirs[8] = {
        "北风", "东北风", "东风", "东南风",
        "南风", "西南风", "西风", "西北风",
    };
    return kDirs[((deg + 23) % 360) / 45];
}

// Open-Meteo current 响应形如
// {"latitude":39.9,"current_units":{...},"current":{"time":"...","temperature_2m":26.1,"weather_code":2,...}}
static esp_err_t weather_parse(const char *body) {
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        ESP_LOGW(TAG, "Weather JSON parse failed");
        return ESP_FAIL;
    }
    esp_err_t err = ESP_FAIL;
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *temp = cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(current, "weather_code");
    const cJSON *hum = cJSON_GetObjectItemCaseSensitive(current, "relative_humidity_2m");
    const cJSON *wind = cJSON_GetObjectItemCaseSensitive(current, "wind_speed_10m");
    const cJSON *dir = cJSON_GetObjectItemCaseSensitive(current, "wind_direction_10m");
    if (cJSON_IsNumber(temp) && cJSON_IsNumber(code)) {
        s_weather.valid = true;
        s_weather.temp_c = (int16_t)((temp->valuedouble >= 0)
                                         ? (temp->valuedouble + 0.5)
                                         : (temp->valuedouble - 0.5));
        s_weather.code = (uint16_t)code->valuedouble;
        s_weather.humidity = cJSON_IsNumber(hum)
                                 ? (uint8_t)hum->valuedouble : 0;
        s_weather.wind_kmh = cJSON_IsNumber(wind)
                                 ? (uint8_t)(wind->valuedouble + 0.5) : 0;
        s_weather.wind_dir = cJSON_IsNumber(dir)
                                 ? (uint16_t)dir->valuedouble % 360 : 0;
        s_version++;
        persist_cache();
        ESP_LOGI(TAG, "Weather updated: %dC code=%u hum=%u%% wind=%ukmh@%u",
                 (int)s_weather.temp_c, (unsigned)s_weather.code,
                 (unsigned)s_weather.humidity, (unsigned)s_weather.wind_kmh,
                 (unsigned)s_weather.wind_dir);
        err = ESP_OK;
    } else {
        ESP_LOGW(TAG, "Weather JSON missing fields");
    }
    cJSON_Delete(root);
    return err;
}

esp_err_t home_weather_refresh(const char *lat, const char *lon) {
    if (!lat || !lon || !lat[0] || !lon[0]) return ESP_ERR_INVALID_ARG;

    char url[192];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast"
             "?latitude=%s&longitude=%s"
             "&current=temperature_2m,weather_code,relative_humidity_2m,wind_speed_10m,wind_direction_10m"
             "&timezone=auto",
             lat, lon);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 1024, /* TLS 接收缓冲,小内存约束 */
        .buffer_size_tx = 512,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGW(TAG, "HTTP client init failed");
        return ESP_FAIL;
    }

    // 小响应(约 300B):open → read 到定长缓冲即可,无需事件处理器。
    char body[RESPONSE_MAX];
    int total = 0;
    esp_err_t err = ESP_FAIL;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        while (total < (int)sizeof(body) - 1) {
            int n = esp_http_client_read(client, body + total, sizeof(body) - 1 - total);
            if (n <= 0) break;
            total += n;
        }
        body[total] = '\0';
        esp_http_client_close(client);
        if (status != 200 || total <= 0) {
            ESP_LOGW(TAG, "Weather HTTP status=%d len=%d", status, total);
        } else {
            err = weather_parse(body);
        }
    } else {
        ESP_LOGW(TAG, "HTTP open failed");
    }
    esp_http_client_cleanup(client);
    return err;
}
