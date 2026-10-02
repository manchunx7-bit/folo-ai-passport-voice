#pragma once

// 小智应用的 NVS 配置读写（命名空间 "xiaozhi"）。
//
// 之所以单独拆一个小模块：ws_url / token / version / client_id 现在有 3 个写入方
// （OTA 激活、串口配置命令、首次生成 UUID），散落的 nvs_open/nvs_close 很容易漏
// nvs_close 造成句柄泄漏，集中在这里只需审一处。

// 纯 C++ 接口（用到 std::string，不能用 extern "C" 链接）。仅供小智应用内部的
// .cc 文件包含。
#include <string>

// 小智配置命名空间
extern const char *const kNsXiaozhi;
// 官方 xiaozhi-esp32 的 websocket 配置命名空间（OTA 下发 url/token/version），
// 若本机 xiaozhi 命名空间缺失则回落到这里，便于直接复用官方后台配置。
extern const char *const kNsWebsocket;

// 读取字符串；不存在或失败时返回 def。
std::string store_get_string(const char *ns, const char *key, const char *def);

// 读取整数；不存在或失败时返回 def。
int store_get_int(const char *ns, const char *key, int def);

// 写入字符串。返回是否成功。
bool store_set_string(const char *ns, const char *key, const std::string &value);

// 写入整数。返回是否成功。
bool store_set_int(const char *ns, const char *key, int value);

// 擦除某个键
bool store_erase(const char *ns, const char *key);
