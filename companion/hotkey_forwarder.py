#!/usr/bin/env python3
"""热键+麦克风转发器: 把 AI Passport 设备变成"无线热键 + 无线麦克风"。

与 relay.py 的差异(架构见 passport-os/docs/voice-udp-channel.md):
  relay  = 收设备音频 → 火山 ASR → 注入文本(需要 ASR 密钥);
  本程序 = 收设备按键事件 → 注入系统热键(如右 Shift,调起语音输入法);
           收设备音频 → 解码 → 写虚拟声卡(VB-Cable),输入法把它当麦克风。
  不需要任何 ASR 密钥/账号 —— 识别由输入法自己完成。

设备端零改动: 「AI语音」应用按住 UP = voice.start + 音频流,松开 = voice.end,
本程序把这对事件映射为热键按下/抬起(或点击,tap 模式)。

用法:
  python companion/hotkey_forwarder.py                  # 右Shift + tap 模式
  python companion/hotkey_forwarder.py --mode hold      # 按住=热键按住
  python companion/hotkey_forwarder.py --key f9         # 换热键
  python companion/hotkey_forwarder.py --list-devices   # 列出声卡,找 CABLE
  python companion/hotkey_forwarder.py --no-audio       # 只做热键转发

虚拟声卡(麦克风路由): 需安装 VB-Audio Virtual Cable(https://vb-audio.com/Cable/),
安装后在语音输入法里把麦克风选为 "CABLE Output"。未安装时本程序仍做热键转发,
但音频无处可去(会打印引导)。
"""

import argparse
import asyncio
import ctypes
import json
import os
import queue
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import adpcm
from udp_transport import UdpTransport, UdpError, EVENT_UUID, AUDIO_UUID

# ---------------------------------------------------------------------------
# Windows 按键注入(ctypes SendInput, 零第三方依赖)
# ---------------------------------------------------------------------------

VK_SCAN = {
    # name: (VK, 扫描码, extended)。扫描码为 PS/2 set-1。
    "right-shift": (0xA1, 0x36, False),
    "left-shift": (0xA0, 0x2A, False),
    "right-ctrl": (0xA3, 0x1D, True),
    "right-alt": (0xA5, 0x38, True),
    "f6": (0x75, 0x40, False),
    "f9": (0x78, 0x43, False),
    "f10": (0x79, 0x44, False),
}

# 完整键位表:在 VK_SCAN 基础上补全 F1-F12/字母/数字/常用键,支持组合键。
# 组合键写法 "win+h"、"ctrl+shift+space"(修饰键在前,普通键最后)。
_KEY_EXTRA = {
    "left-ctrl": (0xA2, 0x1D, False),
    "left-alt": (0xA4, 0x38, False),
    "left-win": (0x5B, 0x5B, True),
    "right-win": (0x5C, 0x5C, True),
    "caps": (0x14, 0x3A, False),
    "space": (0x20, 0x39, False),
    "enter": (0x0D, 0x1C, False),
    "tab": (0x09, 0x0F, False),
    "backspace": (0x08, 0x0E, False),
    "escape": (0x1B, 0x01, False),
}
for _i, _scan in enumerate([0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,
                            0x41, 0x42, 0x43, 0x44, 0x57, 0x58]):
    _KEY_EXTRA[f"f{_i + 1}"] = (0x70 + _i, _scan, False)
for _ch, _scan in zip("qwertyuiopasdfghjklzxcvbnm",
                      [0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                       0x18, 0x19, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
                       0x24, 0x25, 0x26, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,
                       0x31, 0x32]):          # QWERTYUIOP ASDFGHJKL ZXCVBNM
    _KEY_EXTRA[_ch] = (0x41 + (ord(_ch) - 97), _scan, False)   # VK 'A'=0x41
for _i in range(10):
    _d = (_i + 1) % 10                         # '1','2',...,'9','0'
    _KEY_EXTRA[str(_d)] = (0x30 + _d, 0x02 + _i, False)
KEY_MAP = {**VK_SCAN, **_KEY_EXTRA}

# 组合键里的修饰键别名("win+h" 的 win = left-win)
MODIFIER_ALIASES = {"ctrl": "left-ctrl", "alt": "left-alt",
                    "shift": "left-shift", "win": "left-win"}
_MODIFIERS = {"left-ctrl", "right-ctrl", "left-alt", "right-alt",
              "left-shift", "right-shift", "left-win", "right-win"}


def resolve_key_spec(spec):
    """解析热键描述 → [(键名, VK, 扫描码, extended), ...]。

    单键 "right-shift"/"f9"/"h";组合键 "win+h"、"ctrl+shift+space"。
    组合键规则:最后一个必须是普通键,其余都是修饰键。
    """
    parts = [p.strip().lower() for p in spec.split("+") if p.strip()]
    if not parts:
        raise ValueError("热键为空")
    keys = []
    for i, part in enumerate(parts):
        name = MODIFIER_ALIASES.get(part, part)
        if name not in KEY_MAP:
            raise ValueError(f"未知键名 '{part}'")
        if i < len(parts) - 1 and name not in _MODIFIERS:
            raise ValueError(f"组合键里 '{part}' 必须是修饰键(ctrl/alt/shift/win),且放在前面")
        keys.append((name,) + KEY_MAP[name])
    return keys


KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_EXTENDEDKEY = 0x0001
KEYEVENTF_SCANCODE = 0x0008
INPUT_KEYBOARD = 1


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", ctypes.c_ushort),
                ("wScan", ctypes.c_ushort),
                ("dwFlags", ctypes.c_ulong),
                ("time", ctypes.c_ulong),
                ("dwExtraInfo", ctypes.c_void_p)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", ctypes.c_long), ("dy", ctypes.c_long),
                ("mouseData", ctypes.c_ulong), ("dwFlags", ctypes.c_ulong),
                ("time", ctypes.c_ulong), ("dwExtraInfo", ctypes.c_void_p)]


class _INPUTUNION(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT)]   # 尺寸以 MOUSEINPUT 为准


class INPUT(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [("type", ctypes.c_ulong), ("u", _INPUTUNION)]


def attach_to_default_desktop():
    """将当前线程挂接至用户交互桌面 WinSta0\Default(穿透沙箱桌面隔离)。"""
    if sys.platform == "win32":
        try:
            h_desk = ctypes.windll.user32.OpenDesktopW("Default", 0, False, 0x01FF)
            if h_desk:
                ctypes.windll.user32.SetThreadDesktop(h_desk)
        except Exception:
            pass


class _KeyWorker:
    def __init__(self):
        self._q = queue.Queue()
        self._thread = threading.Thread(target=self._run, name="key_worker", daemon=True)
        self._thread.start()

    def _run(self):
        attach_to_default_desktop()
        while True:
            fn, args = self._q.get()
            try:
                fn(*args)
            except Exception as e:
                print(f"[key] Worker error: {e}", file=sys.stderr)
            finally:
                self._q.task_done()

    def post(self, fn, *args):
        self._q.put((fn, args))
        self._q.join()


_worker = None


def _get_worker():
    global _worker
    if _worker is None:
        _worker = _KeyWorker()
    return _worker


def _do_send_key(vk, scan, extended, up):
    flags = (KEYEVENTF_SCANCODE if scan else 0) | (KEYEVENTF_EXTENDEDKEY if extended else 0) | (KEYEVENTF_KEYUP if up else 0)
    inp = INPUT(type=INPUT_KEYBOARD)
    inp.ki = KEYBDINPUT(wVk=vk, wScan=scan, dwFlags=flags, time=0,
                        dwExtraInfo=None)
    ret = ctypes.windll.user32.SendInput(1, ctypes.byref(inp),
                                         ctypes.sizeof(INPUT))
    if ret != 1:
        # SendInput 异常时降级走 keybd_event
        ctypes.windll.user32.keybd_event(vk & 0xFF, scan & 0xFF, flags, 0)


def _send_key(vk, scan, extended, up):
    _get_worker().post(_do_send_key, vk, scan, extended, up)


class KeyInjector:
    """热键注入器(支持单键及组合键,如 right-shift / win+h / ctrl+shift+space)。

    hold=设备按住UP说话期间热键保持按下,松开时抬起(适配"按住说话"类语音输入法,如讯飞/搜狗长按右Shift);
    tap=开启时点击热键(Down+60ms+Up),松开时再次点击以结束录音提交上屏(适配点击式语音键)。
    """

    def __init__(self, spec, mode):
        self.spec = spec
        self.mode = mode
        self._keys = resolve_key_spec(spec)   # [(键名, vk, scan, ext), ...]
        self._held = False
        self._active = False

    def start(self):
        attach_to_default_desktop()
        if self.mode == "hold":
            if not self._held:
                self._held = True
                for _name, vk, scan, ext in self._keys:
                    _send_key(vk, scan, ext, up=False)
                print(f"[key] {self.spec} 按下(hold, 开启语音输入)")
        else:
            if self._active:
                return
            self._active = True
            for _name, vk, scan, ext in self._keys:
                _send_key(vk, scan, ext, up=False)
            time.sleep(0.06)
            for _name, vk, scan, ext in reversed(self._keys):
                _send_key(vk, scan, ext, up=True)
            print(f"[key] {self.spec} 点击(tap, 开启语音输入)")

    def stop(self):
        attach_to_default_desktop()
        if self.mode == "hold" and self._held:
            self._held = False
            for _name, vk, scan, ext in reversed(self._keys):
                _send_key(vk, scan, ext, up=True)
            print(f"[key] {self.spec} 抬起(hold, 结束语音输入)")
        elif self.mode == "tap" and self._active:
            # 满足输入法"按下可开启语音输入，按任意键均可结束"：松开按键时再发一次点击以提交上屏
            self._active = False
            time.sleep(0.08)
            for _name, vk, scan, ext in self._keys:
                _send_key(vk, scan, ext, up=False)
            time.sleep(0.06)
            for _name, vk, scan, ext in reversed(self._keys):
                _send_key(vk, scan, ext, up=True)
            print(f"[key] {self.spec} 再次点击(tap, 结束语音输入)")


# ---------------------------------------------------------------------------
# 音频出口: 解码 ADPCM → 写虚拟声卡(VB-Cable 的 CABLE Input)
# ---------------------------------------------------------------------------

SAMPLE_RATE = 16000
FRAME_BYTES = 3200                      # 1600 样本 × int16
SILENCE = b"\x00" * FRAME_BYTES


class AudioSink:
    """把设备音频块解码后按实时节奏写进虚拟声卡。

    独立写线程 + 有界队列: 写线程 stream.write 天然按实时节拍消费,
    队列只吸收 UDP 抖动;溢出丢最旧(保延迟,宁丢勿积)。
    """

    def __init__(self, device_substr, blocks_max=50):
        self.enabled = False
        self.device_name = None
        self._q = queue.Queue(maxsize=blocks_max)
        try:
            import sounddevice as sd
        except (ImportError, OSError) as e:
            print(f"[audio] sounddevice 不可用({e}),音频转发关闭,仅热键",
                  file=sys.stderr)
            return
        idx, name = self._find_output(sd, device_substr)
        if idx is None:
            print("[audio] 没找到虚拟声卡(含 '%s' 的播放设备)。\n"
                  "  请安装 VB-Audio Virtual Cable: https://vb-audio.com/Cable/\n"
                  "  安装后重新运行本程序,并在语音输入法里把麦克风选为 'CABLE Output'。\n"
                  "  当前仅热键转发生效。" % device_substr, file=sys.stderr)
            return
        self.device_name = name
        try:
            self._stream = sd.RawOutputStream(
                samplerate=SAMPLE_RATE, channels=1, dtype="int16",
                device=idx, blocksize=0)
            self._stream.start()
        except Exception as e:
            print(f"[audio] 打开 '{name}' 失败: {e}", file=sys.stderr)
            return
        self.enabled = True
        threading.Thread(target=self._writer, name="audio_writer",
                         daemon=True).start()
        print(f"[audio] 麦克风路由就绪 → '{name}'"
              " (输入法里把麦克风选为对应的 'CABLE Output')")

    @staticmethod
    def _find_output(sd, substr):
        try:
            default_out = sd.default.output[1] if sd.default.output else -1
        except Exception:
            default_out = -1
        found = (None, None)
        for i, d in enumerate(sd.query_devices()):
            if d["max_output_channels"] > 0 and substr.lower() in d["name"].lower():
                found = (i, d["name"])
                break
        if found[0] is None:
            return (None, None)
        _ = default_out
        return found

    def write_block(self, adpcm_block):
        """AUDIO 载荷 [seq][0x80]+804B → 解码入队。"""
        if not self.enabled:
            return
        try:
            samples = adpcm.decode_block(adpcm_block)
        except Exception as e:
            print(f"[audio] 解码失败: {e}", file=sys.stderr)
            return
        pcm = bytearray(len(samples) * 2)
        for i, v in enumerate(samples):
            v = max(-32768, min(32767, int(v)))
            pcm[i * 2] = v & 0xFF
            pcm[i * 2 + 1] = (v >> 8) & 0xFF
        try:
            self._q.put_nowait(bytes(pcm))
        except queue.Full:
            try:
                self._q.get_nowait()   # 丢最旧,保延迟
                self._q.put_nowait(bytes(pcm))
            except queue.Empty:
                pass

    def _writer(self):
        while True:
            try:
                data = self._q.get(timeout=0.2)
            except queue.Empty:
                data = SILENCE         # 空闲补零,维持实时节拍
            try:
                self._stream.write(data)
            except Exception as e:
                print(f"[audio] 写声卡失败: {e}", file=sys.stderr)
                time.sleep(0.2)


# ---------------------------------------------------------------------------
# 事件/音频 handler + 主循环
# ---------------------------------------------------------------------------

class Forwarder:
    def __init__(self, args):
        self.args = args
        self.injector = KeyInjector(args.key, args.mode)
        # 设备 DOWN 短按发 key.action=enter。它不是语音 PTT 的一部分，
        # 必须始终是一次独立点击，不能复用 hold/tap 模式的主热键实例。
        self.enter_injector = KeyInjector("enter", "tap")
        self.sink = None if args.no_audio else AudioSink(args.output_substr)
        self.audio_blocks = 0

    def on_event(self, payload):
        try:
            ev = json.loads(payload.decode("utf-8").strip())
        except (UnicodeDecodeError, json.JSONDecodeError):
            return
        name = ev.get("event", "")
        if name == "voice.start":
            print("[evt] 设备开始说话(voice.start)")
            self.audio_blocks = 0
            self.injector.start()
        elif name == "voice.end":
            print(f"[evt] 设备结束说话(voice.end),收到音频块={self.audio_blocks}")
            self.injector.stop()
        elif name == "device.hello":
            print("[evt] 设备上线(device.hello)")
        elif name == "key.action" and ev.get("action") == "enter":
            print("[evt] 设备确认发送(key.action=enter)")
            self.enter_injector.start()
        # status/agent.* 等其余事件与本程序无关,忽略

    def on_audio(self, payload):
        # 载荷 = [seq][0x80] + 804B ADPCM block;去掉 2B 帧头再解码
        # --no-audio 模式下电脑使用自己的麦克风，设备音频包直接丢弃。
        if self.sink is not None and len(payload) > 2:
            self.sink.write_block(payload[2:])
            self.audio_blocks += 1
            if self.audio_blocks == 1:
                print("[audio] 已收到设备首个音频块并写入 VB-Cable")

    def on_disconnect(self):
        print("[link] 设备断开,热键复位;等待 beacon 自动重连...")
        self.injector.stop()           # 链路死了热键必须抬起来

    async def run(self):
        while True:
            transport = UdpTransport(port=self.args.port,
                                     device_wait_timeout=10 ** 9,
                                     device_ip=self.args.device_ip)
            status_task = None
            # 链路断开信号:on_disconnect 由读线程经 call_soon_threadsafe 在
            # 事件循环线程里调用,这里 set asyncio.Event 唤醒下方 wait。
            # 修(2026-09-13):原 `await asyncio.Event().wait()` 等的是永远没人
            # set 的匿名事件 —— 设备静默 6s 判定断开后读线程退出、beacon 照发,
            # 本协程却永远卡死 → forwarder 变成"会发 beacon 但聋了"的僵尸,
            # 再也收不到设备(实测复现)。
            link_down = asyncio.Event()

            def on_disconnect():
                self.on_disconnect()
                link_down.set()

            try:
                await transport.scan_for_device(None, None)
                await transport.connect("udp://",
                                        on_disconnect=on_disconnect)
                await transport.start_notify(EVENT_UUID, self.on_event)
                await transport.start_notify(AUDIO_UUID, self.on_audio)
                print("[link] 就绪: 等待设备按键/音频(设备上打开「AI语音」, "
                      "按住 UP 说话)")

                async def status():
                    # 诊断:设备最近一次报文距今多久。稳定链路应始终 <2s(心跳周期)。
                    while True:
                        await asyncio.sleep(5)
                        if transport._device is None:
                            print("[status] 还没发现设备")
                            continue
                        age = time.monotonic() - transport._device_last
                        print(f"[status] 设备 {transport._device[0]} "
                              f"最近报文 {age:.1f}s 前")

                status_task = asyncio.create_task(status())
                await link_down.wait()   # 直到链路断(on_disconnect 已 set)
                link_down.clear()
            except UdpError as e:
                print(f"[link] {e};10s 后重试", file=sys.stderr)
                await asyncio.sleep(10)
            except asyncio.CancelledError:
                return
            finally:
                if status_task:
                    status_task.cancel()
                self.injector.stop()
                try:
                    await transport.disconnect()
                except Exception:
                    pass
            await asyncio.sleep(0.5)


def load_local_config():
    """读 config.local.json(与 relay 共用一份),只取热键转发相关字段。

    缺文件/缺字段都用内置缺省,不强制要求 config 存在。
    使用 fre_state.config_path() 确保兼容 PyInstaller 打包与源码运行。
    """
    try:
        from fre_state import config_path
        path = config_path()
    except Exception:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "config.local.json")
    try:
        with open(path, encoding="utf-8-sig") as f:
            cfg = json.load(f)
        return cfg if isinstance(cfg, dict) else {}
    except Exception:
        return {}


def list_audio_devices():
    try:
        import sounddevice as sd
    except (ImportError, OSError) as e:
        print(f"sounddevice 不可用: {e}")
        return
    for i, d in enumerate(sd.query_devices()):
        kind = "播放" if d["max_output_channels"] > 0 else "录音"
        print(f"  [{i}] {kind} {d['name']}")
    print("提示: 找名字含 'CABLE Input' 的播放设备 = 虚拟声卡已安装。")


def main():
    cfg = load_local_config()
    default_key = str(cfg.get("hotkey_key") or "right-shift")
    default_mode = cfg.get("hotkey_mode")
    if not default_mode:
        # 对 right-shift 等修饰键, 语音识别绝大多数为"按住说话", 缺省使用 hold
        default_mode = "hold" if any(m in default_key.lower() for m in ("shift", "ctrl", "alt")) else "tap"
    else:
        default_mode = str(default_mode)

    ap = argparse.ArgumentParser(
        description="AI Passport 热键+麦克风转发器(配置见 config.local.json, "
                    "命令行参数优先)")
    ap.add_argument("--key", default=default_key,
                    help="注入的热键: 单键(right-shift/f6/h/1...)或组合键"
                         "(win+h / ctrl+shift+space);缺省取配置 hotkey_key")
    ap.add_argument("--mode", default=default_mode,
                    choices=["tap", "hold"],
                    help="tap=设备按下时点击热键(适配点击式语音键); "
                         "hold=按住期间热键保持按下(适配按住式语音输入,如长按右Shift)")
    ap.add_argument("--port", type=int,
                    default=int(cfg["udp_port"]) if str(cfg.get("udp_port") or "").strip().isdigit() else 33333)
    ap.add_argument("--output-substr",
                    default=str(cfg.get("output_substr") or "CABLE Input"),
                    help="虚拟声卡播放设备名包含的子串")
    ap.add_argument("--device-ip",
                    default=str(cfg.get("device_ip") or ""),
                    help="设备目标IP(如 192.168.5.6,防路由器AP隔离阻断广播)")
    ap.add_argument("--no-audio", action="store_true", help="只做热键转发")
    ap.add_argument("--list-keys", action="store_true",
                    help="列出全部可用键名后退出")
    ap.add_argument("--list-devices", action="store_true",
                    help="列出本机声卡后退出")
    args = ap.parse_args()

    if args.list_keys:
        print("可用键名(组合键用 + 连接,如 win+h):")
        names = sorted(KEY_MAP)
        width = max(len(n) for n in names) + 2
        for i in range(0, len(names), 6):
            print("  " + "".join(n.ljust(width) for n in names[i:i + 6]))
        print("修饰键: ctrl/alt/shift/win (left-/right- 前缀区分左右)")
        return

    if args.list_devices:
        list_audio_devices()
        return

    try:
        resolve_key_spec(args.key)
    except ValueError as e:
        ap.error(f"{e};用 --list-keys 查看全部键名")

    fwd = Forwarder(args)
    print(f"热键: {args.key}  模式: {args.mode}  "
          f"音频: {'关闭' if args.no_audio else ('→ ' + fwd.sink.device_name if fwd.sink.enabled else '未就绪(装 VB-Cable 后重启本程序)')}")
    try:
        asyncio.run(fwd.run())
    except KeyboardInterrupt:
        print("\n退出")
        fwd.injector.stop()


if __name__ == "__main__":
    main()
