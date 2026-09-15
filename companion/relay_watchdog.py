"""电脑端语音中转守护：relay.py 退出后 2 秒自动重跑。

为什么需要它：relay.py v1 断链即退出 —— 设备静默 6s(3 个 PING 周期)
→ `_handle_disconnect()` → `run()` 返回 → 进程结束。而设备侧是全自动重连的
(beacon 一回来就 link up),两端不对称,所以空闲一会儿电脑端就"掉线"了。

用法(在 companion 目录下,用 venv 的 python):
    .venv\\Scripts\\python.exe -u relay_watchdog.py

停止: Ctrl+C(会先结束当前 relay,再在 Ctrl+C 时一起退出)。
"""
import subprocess
import sys
import time

RELAY = "relay.py"


def main() -> None:
    while True:
        print("[watchdog] ---- relay 启动 ----", flush=True)
        try:
            subprocess.run([sys.executable, "-u", RELAY])
        except KeyboardInterrupt:
            print("[watchdog] 收到中断,退出", flush=True)
            return
        print("[watchdog] relay 已退出, 2s 后重启", flush=True)
        time.sleep(2)


if __name__ == "__main__":
    main()
