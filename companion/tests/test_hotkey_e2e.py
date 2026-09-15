#!/usr/bin/env python3
"""hotkey_forwarder 端到端自测(单脚本版)。

顺序: 起 hotkey_forwarder 子进程(-u 无缓冲) → 装 WH_KEYBOARD_LL 钩子 →
模拟设备发包(hello/start/10块正弦ADPCM/end) → 收钩子 → 断言右Shift
按下+抬起且带 INJECTED 标志。

用法: python tests/test_hotkey_e2e.py
"""
import ctypes
import ctypes.wintypes
import json
import math
import os
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import adpcm  # noqa: E402

COMPANION = os.path.dirname(os.path.abspath(__file__)) + "/.."
TARGET = ("127.0.0.1", 33333)
CAPTURE_SECONDS = 5.0

user32 = ctypes.windll.user32
WH_KEYBOARD_LL = 13
WM_KEYDOWN, WM_KEYUP = 0x0100, 0x0101
LLKHF_INJECTED = 0x10


class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [("vkCode", ctypes.c_ulong), ("scanCode", ctypes.c_ulong),
                ("flags", ctypes.c_ulong), ("time", ctypes.c_ulong),
                ("dwExtraInfo", ctypes.c_void_p)]


captured = []
_hook = None
_proc_ref = None          # 钩子回调必须保持引用,否则被 GC 后钩子静默失效
_installed = threading.Event()

# x64 下 LRESULT/WPARAM 都是 8 字节,签名错了钩子回调不会被正确调用
LRESULT = ctypes.c_ssize_t
HOOKPROC = ctypes.WINFUNCTYPE(LRESULT, ctypes.c_int,
                              ctypes.c_ssize_t,
                              ctypes.POINTER(KBDLLHOOKSTRUCT))
user32.CallNextHookEx.restype = LRESULT


def hook_proc(n_code, w_param, l_param):
    if n_code == 0:
        kb = l_param.contents
        captured.append((kb.vkCode, kb.scanCode, w_param,
                         bool(kb.flags & LLKHF_INJECTED)))
    return user32.CallNextHookEx(None, n_code, w_param, l_param)


def attach_to_default_desktop():
    if sys.platform == "win32":
        try:
            h_desk = user32.OpenDesktopW("Default", 0, False, 0x01FF)
            if h_desk:
                user32.SetThreadDesktop(h_desk)
        except Exception:
            pass


def install_hook():
    """WH_KEYBOARD_LL 要求:安装与消息泵必须在同一线程,否则一个事件都收不到。"""
    global _hook, _proc_ref

    def _hook_thread():
        global _hook, _proc_ref
        attach_to_default_desktop()
        _proc_ref = HOOKPROC(hook_proc)
        _hook = user32.SetWindowsHookExW(WH_KEYBOARD_LL, _proc_ref, None, 0)
        if not _hook:
            print(f"[hook] 安装失败 err={ctypes.GetLastError()}",
                  file=sys.stderr)
            _installed.set()
            return
        _installed.set()
        msg = ctypes.wintypes.MSG()
        while _hook:
            if user32.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):
                user32.TranslateMessage(ctypes.byref(msg))
                user32.DispatchMessageW(ctypes.byref(msg))
            time.sleep(0.01)

    threading.Thread(target=_hook_thread, name="hook_thread",
                     daemon=True).start()
    _installed.wait(timeout=2)
    return bool(_hook)


def fake_device():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    time.sleep(1.5)

    def ev(obj):
        s.sendto(bytes([0x02]) + (json.dumps(obj) + "\n").encode("utf-8"),
                 TARGET)

    ev({"event": "device.hello", "proto": 1})
    time.sleep(0.3)
    ev({"event": "voice.start", "audio": "ima_adpcm"})
    st = adpcm.AdpcmState()
    for seq in range(10):
        pcm = [int(8000 * math.sin(2 * math.pi * 440 * i / 16000))
               for i in range(1600)]
        s.sendto(bytes([0x01, seq & 0xFF, 0x80]) + adpcm.encode_block(st, pcm),
                 TARGET)
        time.sleep(0.1)
    ev({"event": "voice.end"})
    print("[fake-device] 已发送 hello/start/10块音频/end", flush=True)


def main():
    base = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
    env = dict(os.environ, PYTHONUNBUFFERED="1")
    fwd = subprocess.Popen(
        [sys.executable, os.path.join(base, "hotkey_forwarder.py"),
         "--no-audio", "--key", "right-shift", "--mode", "tap"],
        cwd=base, env=env)
    time.sleep(3)
    try:
        if not install_hook():
            return 2
        fake_device()
        time.sleep(CAPTURE_SECONDS - 1.5)

        shifts = [e for e in captured
                  if e[0] in (0xA0, 0xA1) or e[1] in (0x2A, 0x36)]
        print(f"[hook] 捕获键盘事件 {len(captured)} 个, Shift 类 {len(shifts)} 个:")
        for vk, scan, w_param, injected in shifts:
            kind = "DOWN" if w_param == WM_KEYDOWN else "UP"
            print(f"    vk=0x{vk:02X} scan=0x{scan:02X} {kind} "
                  f"{'INJECTED' if injected else 'REAL'}")

        downs = [e for e in shifts
                 if e[2] == WM_KEYDOWN and (e[0] == 0xA1 or e[1] == 0x36)]
        ups = [e for e in shifts
               if e[2] == WM_KEYUP and (e[0] == 0xA1 or e[1] == 0x36)]
        injected_all = bool(shifts) and all(e[3] for e in shifts)
        if len(downs) >= 1 and len(ups) >= 1 and injected_all:
            print("[PASS] 右Shift 注入验证通过(按下+抬起,均带 INJECTED 标志)")
            return 0
        print("[FAIL] 未捕获到预期的右Shift 注入事件")
        return 1
    finally:
        user32.UnhookWindowsHookEx(_hook)
        fwd.terminate()


if __name__ == "__main__":
    sys.exit(main())
