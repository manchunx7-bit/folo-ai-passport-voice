#include "apps/xiaozhi/xiaozhi_store.h"

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <cstring>

static const char *TAG = "xiaozhi_store";

const char *const kNsXiaozhi = "xiaozhi";
const char *const kNsWebsocket = "websocket";

namespace {

// NVS 的 str 读取需要两步，且长度含结尾 '\0'。这里统一处理并保证返回的
// std::string 不含结尾 NUL —— 否则拼进 HTTP 头/URL 会多一个不可见字节。
bool read_str(nvs_handle_t h, const char *key, std::string &out) {
    size_t len = 0;
    esp_err_t err = nvs_get_str(h, key, nullptr, &len);
    if (err != ESP_OK || len == 0) return false;

    std::string tmp(len, '\0');
    err = nvs_get_str(h, key, tmp.data(), &len);
    if (err != ESP_OK) return false;

    while (!tmp.empty() && tmp.back() == '\0') tmp.pop_back();
    if (tmp.empty()) return false;
    out = std::move(tmp);
    return true;
}

} // namespace

std::string store_get_string(const char *ns, const char *key, const char *def) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return def ? std::string(def) : std::string();

    std::string value;
    bool ok = read_str(h, key, value);
    nvs_close(h);
    if (!ok) return def ? std::string(def) : std::string();
    return value;
}

int store_get_int(const char *ns, const char *key, int def) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return def;

    int32_t value = 0;
    esp_err_t err = nvs_get_i32(h, key, &value);
    nvs_close(h);
    return (err == ESP_OK) ? (int)value : def;
}

bool store_set_string(const char *ns, const char *key, const std::string &value) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed", ns);
        return false;
    }
    esp_err_t err = nvs_set_str(h, key, value.c_str());
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "nvs_set_str(%s/%s) failed: %s", ns, key, esp_err_to_name(err));
    return err == ESP_OK;
}

bool store_set_int(const char *ns, const char *key, int value) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_i32(h, key, (int32_t)value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool store_erase(const char *ns, const char *key) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}
