#pragma once

// 小智后台 OTA / 设备激活客户端。
//
// 为什么必须要有这一层：官方后台（api.tenclass.net）不会白送一个可用的
// WebSocket 凭据。设备必须先用 Device-Id/Client-Id 调 OTA 接口，服务端才会返回
//
//   * websocket{url, token, version}  —— 真正的对话地址与令牌
//   * activation{code, message}       —— 设备还没绑定，要求用户去后台绑定这个 MAC
//
// 官方固件（xiaozhi-esp32 ota.cc / application.cc）就是这么做的。之前的实现直接
// 硬编码 wss://api.tenclass.net/xiaozhi/v1/ + "test-token"，跳过整个激活流程，
// 在官方后台下"连得上但对话没有结果"，根因就在这里。
//
// 自建服务端（xiaozhi-esp32-server）通常也提供同一个 OTA 路径，因此这套流程对
// 自建后台同样适用。

#include <string>

namespace xiaozhi {

enum class OtaResult {
    Ok,               // 拿到了 websocket 配置
    NeedsActivation,  // 服务端要求先绑定设备（带激活码）
    NoConfig,         // 响应正常但没有 websocket 段（用本地 NVS / 默认值继续）
    NetworkError,     // 请求失败（无网、TLS、超时）
    ParseError,       // 响应无法解析
};

struct OtaOutcome {
    OtaResult result{OtaResult::NetworkError};
    std::string url;                  // websocket url
    std::string token;                // websocket token
    int version{0};                   // 协议版本（0 = 服务端未指定）
    std::string activation_code;      // 激活码（NeedsActivation 时）
    std::string activation_message;   // 服务端给的提示文案
    std::string detail;               // 诊断信息 / HTTP 状态码
};

// 用 /xiaozhi/ota/ 换配置。若成功会顺带把 url/token/version 写入 NVS 命名空间
// "websocket"（与官方固件同构），并通过 xiaozhi_store 的 "xiaozhi" 命名空间暴露。
//
// 必须在控制任务里调用（内部会做一次带 TLS 的 HTTPS 请求，耗时数百毫秒）。
OtaOutcome ota_fetch_config();

// 服务端返回的 HTTP 状态码（诊断用，0 表示没拿到响应）
int ota_last_http_status();

} // namespace xiaozhi
