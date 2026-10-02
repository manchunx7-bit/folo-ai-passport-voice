#include "apps/xiaozhi/xiaozhi_ota.h"
#include "apps/xiaozhi/xiaozhi_store.h"

#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <cJSON.h>
#include <cstring>
#include <ctime>
#include <sys/time.h>

static const char *TAG = "xiaozhi_ota";

namespace xiaozhi {

namespace {

constexpr int kHttpTimeoutMs = 10000;
constexpr size_t kRespCap = 1024;   // 官方 OTA 响应实测约 500 字节
constexpr const char *kDefaultOtaUrl = "https://api.tenclass.net/xiaozhi/ota/";

int s_last_http_status = 0;

// 必须放在【静态存储】而不是栈上：这一层已经要跑 mbedTLS 握手 + NVS/SPI-Flash
// 读取，本身就是设备上最深的调用链；再往栈上放 2KB 缓冲会直接把控制任务压爆
// （真机实测：栈指针越过栈下限 40 字节 → "Stack protection fault" → 重启）。
// 该函数只在控制任务里被调用，因此单个静态实例是安全的。
struct RespBuf {
    char data[kRespCap];
    size_t len;
};
RespBuf s_resp;

esp_err_t HttpEventHandler(esp_http_client_event_t *evt) {
    auto *buf = static_cast<RespBuf *>(evt->user_data);
    if (evt->event_id == HTTP_EVENT_ON_DATA && buf != nullptr && evt->data != nullptr) {
        size_t space = kRespCap - 1 - buf->len;
        size_t copy = ((size_t)evt->data_len > space) ? space : (size_t)evt->data_len;
        if (copy > 0) {
            std::memcpy(buf->data + buf->len, evt->data, copy);
            buf->len += copy;
            buf->data[buf->len] = '\0';
        }
    }
    return ESP_OK;
}

std::string MacString() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

// 官方协议:每台设备一个稳定的 Client-Id(UUID v4),首次启动生成并持久化。
// 空着也能拿到验证码(服务器按 Device-Id 兜底),但绑定/后续交互都以
// Client-Id 为身份锚点,照规范补齐。
std::string EnsureClientId() {
    std::string id = store_get_string(kNsXiaozhi, "client_id", "");
    if (!id.empty()) return id;

    uint8_t raw[16] = {0};
    esp_fill_random(raw, sizeof(raw));
    raw[6] = (raw[6] & 0x0Fu) | 0x40u; /* version 4 */
    raw[8] = (raw[8] & 0x3Fu) | 0x80u; /* variant 10xx */
    char buf[37];
    std::snprintf(buf, sizeof(buf),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
                  raw[8], raw[9], raw[10], raw[11], raw[12], raw[13], raw[14], raw[15]);
    id = buf;
    store_set_string(kNsXiaozhi, "client_id", id);
    ESP_LOGI(TAG, "Generated Client-Id: %s", id.c_str());
    return id;
}

void ApplyServerTime(const cJSON *root) {
    const cJSON *st = cJSON_GetObjectItem(root, "server_time");
    if (!cJSON_IsObject(st)) return;
    const cJSON *ts = cJSON_GetObjectItem(st, "timestamp");
    if (!cJSON_IsNumber(ts)) return;

    // 官方这里给的是毫秒；也有服务端给秒。两种都兜住。
    double raw = ts->valuedouble;
    time_t secs = (raw > 1e11) ? (time_t)(raw / 1000.0) : (time_t)raw;
    if (secs < 1600000000) return;   // 明显不合理的值就不设了

    struct timeval tv = {};
    tv.tv_sec = secs;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    ESP_LOGI(TAG, "System time set from server: %lld", (long long)secs);
}

// 上报设备信息，官方服务端据此识别机型/版本。字段取自官方 Board::GetJson() 的子集。
std::string BuildBoardJson(const std::string &client_id) {
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) return "{}";

    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddStringToObject(root, "mac_address", MacString().c_str());
    cJSON_AddStringToObject(root, "uuid", client_id.c_str());
    cJSON_AddStringToObject(root, "chip_model_name", "esp32c3");
    cJSON_AddNumberToObject(root, "flash_size", 8 * 1024 * 1024);
    cJSON_AddNumberToObject(root, "minimum_free_heap_size",
                            (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    cJSON *app_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(app_obj, "name", "passport-os");
    cJSON_AddStringToObject(app_obj, "version", "1.0.0");
    cJSON_AddStringToObject(app_obj, "idf_version", esp_get_idf_version());
    cJSON_AddItemToObject(root, "application", app_obj);

    char *printed = cJSON_PrintUnformatted(root);
    std::string out = printed ? printed : "{}";
    if (printed) cJSON_free(printed);
    cJSON_Delete(root);
    return out;
}

} // namespace

int ota_last_http_status() { return s_last_http_status; }

OtaOutcome ota_fetch_config() {
    OtaOutcome out;
    s_last_http_status = 0;

    const std::string client_id = EnsureClientId();
    const std::string mac = MacString();
    const std::string ota_url = store_get_string(kNsXiaozhi, "ota_url", kDefaultOtaUrl);
    const std::string body = BuildBoardJson(client_id);
    const std::string auth = "Device-Id: " + mac + "\r\nClient-Id: " + client_id + "\r\n";

    ESP_LOGI(TAG, "POST %s (free heap %u)", ota_url.c_str(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    RespBuf &resp = s_resp;
    resp.len = 0;
    resp.data[0] = '\0';

    esp_http_client_config_t cfg = {};
    cfg.url = ota_url.c_str();
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = kHttpTimeoutMs;
    cfg.event_handler = HttpEventHandler;
    cfg.user_data = &resp;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.keep_alive_enable = false;
    cfg.buffer_size = 1024;
    cfg.buffer_size_tx = 1024;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        out.result = OtaResult::NetworkError;
        out.detail = "http client init failed";
        return out;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Device-Id", mac.c_str());
    esp_http_client_set_header(client, "Client-Id", client_id.c_str());
    esp_http_client_set_post_field(client, body.c_str(), (int)body.size());

    esp_err_t err = esp_http_client_perform(client);
    s_last_http_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        out.result = OtaResult::NetworkError;
        out.detail = std::string(esp_err_to_name(err)) + " (http " +
                     std::to_string(s_last_http_status) + ")";
        ESP_LOGW(TAG, "OTA request failed: %s", out.detail.c_str());
        return out;
    }
    if (s_last_http_status != 200) {
        out.result = OtaResult::NetworkError;
        out.detail = "HTTP " + std::to_string(s_last_http_status);
        ESP_LOGW(TAG, "OTA returned %s", out.detail.c_str());
        return out;
    }

    ESP_LOGD(TAG, "OTA response (%u bytes): %.200s", (unsigned)resp.len, resp.data);

    cJSON *root = cJSON_ParseWithLength(resp.data, resp.len);
    if (root == nullptr) {
        out.result = OtaResult::ParseError;
        out.detail = "invalid JSON";
        ESP_LOGW(TAG, "OTA response is not JSON: %.120s", resp.data);
        return out;
    }

    ApplyServerTime(root);

    // 1) 激活段：设备还没被绑定
    const cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation)) {
        const cJSON *code = cJSON_GetObjectItem(activation, "code");
        const cJSON *msg = cJSON_GetObjectItem(activation, "message");
        if (cJSON_IsString(code) && code->valuestring) out.activation_code = code->valuestring;
        if (cJSON_IsString(msg) && msg->valuestring) out.activation_message = msg->valuestring;
        out.result = OtaResult::NeedsActivation;
        ESP_LOGW(TAG, "Device needs activation. code='%s' message='%s'",
                 out.activation_code.c_str(), out.activation_message.c_str());
        cJSON_Delete(root);
        return out;
    }

    // 2) websocket 段：真正的对话凭据
    const cJSON *ws = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(ws)) {
        const cJSON *url = cJSON_GetObjectItem(ws, "url");
        const cJSON *token = cJSON_GetObjectItem(ws, "token");
        const cJSON *ver = cJSON_GetObjectItem(ws, "version");
        if (cJSON_IsString(url) && url->valuestring) out.url = url->valuestring;
        if (cJSON_IsString(token) && token->valuestring) out.token = token->valuestring;
        if (cJSON_IsNumber(ver)) out.version = ver->valueint;

        if (!out.url.empty()) {
            // 同时落到官方同名命名空间，便于与官方固件/后台配置互认
            store_set_string(kNsWebsocket, "url", out.url);
            if (!out.token.empty()) store_set_string(kNsWebsocket, "token", out.token);
            if (out.version > 0) store_set_int(kNsWebsocket, "version", out.version);
            // 本应用自己的命名空间（优先读取）
            store_set_string(kNsXiaozhi, "ws_url", out.url);
            if (!out.token.empty()) store_set_string(kNsXiaozhi, "token", out.token);
            if (out.version > 0) store_set_int(kNsXiaozhi, "version", out.version);

            out.result = OtaResult::Ok;
            ESP_LOGI(TAG, "Got websocket config: url=%s version=%d token=%s",
                     out.url.c_str(), out.version, out.token.empty() ? "(none)" : "(set)");
            cJSON_Delete(root);
            return out;
        }
    }

    const cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (cJSON_IsObject(firmware)) {
        const cJSON *fv = cJSON_GetObjectItem(firmware, "version");
        ESP_LOGW(TAG, "Server offers firmware %s; auto-update not implemented",
                 (cJSON_IsString(fv) && fv->valuestring) ? fv->valuestring : "?");
    }

    out.result = OtaResult::NoConfig;
    out.detail = "no websocket section";
    ESP_LOGW(TAG, "OTA response has no websocket section");
    cJSON_Delete(root);
    return out;
}

} // namespace xiaozhi
