# 参与开发

先阅读 `docs/DEVELOPMENT.zh-CN.md`。反馈使用问题请填写随身语音 Issue 模板。

修改电脑端后运行 `python -m unittest discover -s windows/tests -p "test_*.py" -v`；
修改烧录脚本后运行 `python -m unittest discover -s tools -p "test_*.py" -v`；
固件在 ESP-IDF 5.5.3 环境构建，并运行 `bash source/firmware/tools/test-host.sh`。

PR 请说明问题、改动、验证结果和未验证部分。音频相关改动请区分模拟测试、回环录音与真实输入法出字。
请保留许可证，不提交 Wi-Fi 密码、API Key、个人配置、录音、完整 Flash 备份或开发缓存。
