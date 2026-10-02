// 手机设置服务:热点期间唯一的 HTTP 服务器，连接网络后继续填写资料。
// 手机连设备热点并打开 192.168.4.1，可配网、设置资料和上传图片。
// 图片缩放与 RGB565 编码全部在浏览器端完成,设备端零图片解码开销。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 启动配置服务(幂等)。需先 StartConfigAp(true)，返回后可获取热点地址。
esp_err_t home_upload_start(void);

// 停止服务并回收 httpd 内存(退出上传模式/离开首页时必须调用)。
void home_upload_stop(void);

bool home_upload_active(void);
bool home_upload_busy(void);
bool home_upload_network_verified(void);
bool home_upload_finished(void);

// "http://192.168.x.x"(仅服务运行期间有效)
const char *home_upload_url(void);

#ifdef __cplusplus
}
#endif
