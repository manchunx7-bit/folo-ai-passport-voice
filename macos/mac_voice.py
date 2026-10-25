#!/usr/bin/env python3
"""Apple Silicon test client; reuses the current Wi-Fi/ADPCM device protocol."""
import argparse
import asyncio
import ipaddress
import json
from pathlib import Path
import platform
import signal
import socket
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'windows'))
import hotkey_forwarder as shared
from udp_transport import UdpTransport
from mac_keys import CODES, MacKeyInjector, QuartzKeyboard, KeyErrorMac, resolve_key_spec
from mac_audio import make_audio_sink, coreaudio_options
from mac_network import broadcast_targets


class MacForwarder(shared.Forwarder):
    def __init__(self, args, keyboard, sink):
        # Avoid constructing any Windows key injector or audio endpoint.
        self.args = args
        self.injector = MacKeyInjector(args.key, args.mode, keyboard)
        self.enter_injector = MacKeyInjector('enter', 'tap', keyboard)
        self.sink = sink
        self.audio_route_label = 'BlackHole'
        self.audio_blocks = 0
        self.active = False
        self.last_audio_at = 0.0
        self.last_seq = None
        self.last_enter_at = float('-inf')

    def make_transport(self):
        return UdpTransport(port=self.args.port, device_wait_timeout=10 ** 9,
            device_ip=self.args.device_ip, auto_recover=False, broadcast_targets=broadcast_targets)

    def pc_status(self):
        return {'type': 'pc.status', 'audioReady': bool(self.sink and self.sink.enabled),
                'hotkeyReady': bool(self.injector.ready)}

    def finish(self, drain=False):
        try:
            super().finish(drain=drain)
        except KeyErrorMac as error:
            print(f'[key] 释放或结束快捷键失败：{error}', file=sys.stderr)

    def on_event(self, payload):
        try:
            event = json.loads(payload.decode('utf-8').strip())
            if isinstance(event, dict) and (event.get('event') == 'voice.start' or
                    event.get('event') == 'key.action') and not self.injector.ready:
                self.finish()
                return
            super().on_event(payload)
        except KeyErrorMac as error:
            self.finish()
            print(f'[key] Mac 快捷键未就绪：{error}', file=sys.stderr)
        except (ValueError, UnicodeDecodeError):
            return

    def check_health(self, now=None):
        if self.active and not self.injector.ready:
            self.finish()
        super().check_health(now)

    def on_audio(self, payload):
        if self.active and not self.injector.ready:
            self.finish()
            return
        super().on_audio(payload)


def load_config(path):
    source = Path(path)
    if not source.exists() and source == HERE / 'voice-config.json':
        source = HERE / 'voice-config.example.json'
    with source.open(encoding='utf-8-sig') as stream:
        config = json.load(stream)
    if not isinstance(config, dict):
        raise ValueError('配置必须是 JSON 对象')
    return config


def diagnose(args, keyboard_factory=QuartzKeyboard, sd=None):
    print('AI Passport Mac 测试版诊断（不注入按键、不采音、不更改系统音频设置）')
    print('系统：', platform.mac_ver()[0] or sys.platform, '架构：', platform.machine())
    failures = 0
    if sys.platform != 'darwin' or platform.machine() not in ('arm64', 'x86_64') or sys.version_info < (3, 11):
        print('[FAIL] 请在原生 arm64/x86_64 Python 3.11+ 运行（勿用 Rosetta）。')
        failures += 1
    try:
        keyboard = keyboard_factory()
        if not keyboard.ready:
            raise RuntimeError('请在隐私与安全→辅助功能授权实际启动程序，然后重开客户端')
        print('[OK] 按键权限；仍需核对微信输入法的语音快捷键。')
    except Exception as error:
        print('[FAIL]', error)
        failures += 1
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('0.0.0.0', args.port))
        print(f'[OK] UDP {args.port} 可绑定；这不证明局域网权限或设备链路已通。')
    except OSError as error:
        print('[FAIL] UDP 端口不可用；请只运行一个转发程序：', error)
        failures += 1
    try:
        module = sd
        if module is None:
            import sounddevice as module
        devices = module.query_devices()
        candidates = [(i, d) for i, d in enumerate(devices)
            if args.output_substr.lower() in d['name'].lower() and d['max_output_channels'] >= 2]
        valid = []
        for index, device in candidates:
            if device['max_input_channels'] < 2:
                continue
            try:
                module.check_output_settings(device=index, samplerate=16000, channels=2,
                    dtype='int16', extra_settings=coreaudio_options(module))
                valid.append(device['name'])
            except Exception:
                pass
        if not valid:
            raise RuntimeError('未找到支持格式的双向虚拟声卡，请单独安装 BlackHole 2ch')
        print('[OK] 可用虚拟声卡：', ', '.join(valid))
    except Exception as error:
        print('[FAIL] 音频：', error)
        failures += 1
    print('诊断通过不代表微信输入法出字成功；请在空白文稿实测按下、说话、松开。')
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description='AI Passport · Apple 芯片 Mac / 微信输入法测试客户端')
    parser.add_argument('--config', default=str(HERE / 'voice-config.json'))
    parser.add_argument('--key')
    parser.add_argument('--mode', choices=['hold', 'tap'])
    parser.add_argument('--port', type=int)
    parser.add_argument('--device-ip')
    parser.add_argument('--output-substr')
    parser.add_argument('--diagnose', action='store_true')
    parser.add_argument('--list-keys', action='store_true')
    parser.add_argument('--list-devices', action='store_true')
    args = parser.parse_args()
    if args.list_keys:
        print(' '.join(sorted(CODES)))
        print('修饰键：ctrl/shift/cmd/option；与微信输入法设置保持一致。Fn 为待实测兼容项。')
        return 0
    try:
        config = load_config(args.config)
        args.key = args.key or config.get('hotkey_key') or 'f9'
        args.mode = args.mode or config.get('hotkey_mode') or 'hold'
        args.port = args.port if args.port is not None else int(config.get('udp_port', 33333))
        args.device_ip = args.device_ip if args.device_ip is not None else str(config.get('device_ip') or '')
        args.output_substr = args.output_substr or config.get('output_substr') or 'BlackHole 2ch'
        resolve_key_spec(args.key)
        if args.mode not in ('hold', 'tap') or not 1 <= args.port <= 65535:
            raise ValueError('快捷键模式或 UDP 端口无效')
        if not isinstance(args.output_substr, str) or not args.output_substr.strip():
            raise ValueError('虚拟声卡名称不能为空')
        if args.device_ip:
            ipaddress.IPv4Address(args.device_ip)
    except (OSError, ValueError, TypeError) as error:
        parser.error(str(error))
    if args.diagnose:
        return diagnose(args)
    if sys.platform != 'darwin' or platform.machine() not in ('arm64', 'x86_64') or sys.version_info < (3, 11):
        parser.error('请在原生 arm64/x86_64 Python 3.11+ 中运行')
    if args.list_devices:
        try:
            import sounddevice as sd
            for i, d in enumerate(sd.query_devices()):
                print(i, d['name'], '输入', d['max_input_channels'], '输出', d['max_output_channels'])
        except (ImportError, OSError) as error:
            parser.error(f'无法枚举声卡，请先运行 install.command：{error}')
        return 0
    try:
        keyboard = QuartzKeyboard()
    except ImportError as error:
        parser.error(f'缺少 Mac 依赖，请先运行 install.command：{error}')
    sink = make_audio_sink(shared, args.output_substr)
    forwarder = MacForwarder(args, keyboard, sink)
    print('Mac 测试客户端：设备与 Mac 连接同一局域网，进入设备的 AI语音。')
    print(f'快捷键 {args.key} / {args.mode}，请先在微信输入法里设为一致。')
    print('微信输入法的语音麦克风选择 BlackHole 2ch；如只跟随系统输入，需自行在系统声音输入中选择。')
    print('本程序不更改默认扬声器或麦克风。按 Ctrl+C 停止。')
    if not keyboard.ready:
        print('[权限未就绪] 请运行 diagnose.command，授权后重开。')
    def stop_signal(_signum, _frame):
        raise KeyboardInterrupt
    for name in ('SIGTERM', 'SIGHUP'):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), stop_signal)
    try:
        asyncio.run(forwarder.run())
    except KeyboardInterrupt:
        pass
    finally:
        forwarder.finish()
        sink.close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
