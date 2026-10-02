# 开源来源与第三方组件

本项目为社区衍生作品，不是 FoloToy、腾讯或 VB-Audio 的官方产品。
`LICENSE` 中保留 FoloToy 的 MIT 版权声明；第三方组件遵循各自许可，不因主项目 MIT 而改变。

| 部分 | 来源 / 许可说明 |
| --- | --- |
| AI Passport 硬件参考与基础代码 | https://github.com/FoloToy/ai-passport ，MIT |
| 随身语音基础实现 | https://github.com/zhaohuaxiaoy/folo-ai-passport-voice ，MIT |
| Passport OS 社区整机代码 | https://github.com/manchunx7-bit/passport-os ，保留来源声明 |
| ESP-IDF 5.5.3 | https://github.com/espressif/esp-idf ，Apache-2.0 及所含组件各自许可 |
| LVGL 9.5.0 | https://github.com/lvgl/lvgl ，MIT；其可选库和字体遵循各自许可 |
| Espressif button / codec / audio codec / LVGL port / websocket 等 | 精确版本见 `source/firmware/dependencies.lock`；许可证原文随附 `licenses/` |
| esp-wifi-connect | https://github.com/78/esp-wifi-connect ，MIT，来源信息在组件的 `idf_component.yml` |
| 中文与数字字体 | Source Han Sans SC、Montserrat 等；随附现有字体许可证，子集只包含固件实际字形 |
| Windows 运行时 | Python、sounddevice、CFFI、PortAudio、PyInstaller bootloader；可获取的许可证原文随附 `licenses/` |
| VB-CABLE | https://vb-audio.com/Cable/ ，独立第三方产品；驱动不随本包分发，由用户从官网安装 |
| 微信输入法 | 独立第三方软件，不随本包分发；快捷键、识别服务和版本行为由厂商提供 |

图像、音频和个人资料通过设备运行时配置；本包不复制维护者设备中的头像、名片、Wi-Fi 或录音。
默认固件中已有的几何图形、字体子集和游戏资源保留在对应源文件，不宣称是本次新创作素材。
如发现来源或许可遗漏，请提供具体文件路径供维护者核对。
