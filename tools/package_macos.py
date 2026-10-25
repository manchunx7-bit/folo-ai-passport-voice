"""Build an explicit source-only Mac test ZIP; no drivers, config or firmware."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FILES = (
    'LICENSE', 'THIRD_PARTY_NOTICES.md',
    'macos/README.zh-CN.md', 'macos/mac_voice.py', 'macos/mac_audio.py',
    'macos/mac_keys.py', 'macos/mac_network.py', 'macos/requirements.txt',
    'macos/platform_support.py',
    'macos/install.command', 'macos/start.command', 'macos/diagnose.command',
    'macos/voice-config.example.json', 'macos/tests/test_mac_client.py',
    'macos/tests/test_platform_support.py',
    'windows/adpcm.py', 'windows/hotkey_forwarder.py', 'windows/udp_transport.py',
    'windows/tests/test_hotkey_forwarder_unit.py', 'windows/tests/test_hotkey_release.py',
)
PREFIX = 'AI-Passport-Voice-Mac-test/'


def build(output):
    payloads = {name: (ROOT / name).read_bytes() for name in FILES}
    # The root quick start must link only to the included guide, not assume repo files.
    payloads['README-Mac.md'] = ('# AI Passport Mac 测试包\n\n'
        '先阅读 [Mac 安装与验收教程](macos/README.zh-CN.md)。\n\n'
        '在 Intel 或 Apple 芯片 Mac 解压后依次运行 macos/install.command、'
        'macos/diagnose.command、macos/start.command。\n\n'
        '这是源码测试包，未完成 Mac 实机和微信输入法真实出字验收。'
        '不包含固件，也不需要为此次适配重新烧录。\n').encode('utf-8')
    manifest = dict(package='macos-native-source-test', hardware_validated=False,
        notes='Local fake-native and real-loopback tests only; no signed app or drivers.',
        sha256={name: hashlib.sha256(data).hexdigest() for name, data in payloads.items()})
    payloads['MANIFEST.json'] = (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in payloads.items():
            info = zipfile.ZipInfo(PREFIX + name, date_time=(2026, 10, 17, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (0o100755 if name.endswith('.command') else 0o100644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
        assert set(archive.namelist()) == {PREFIX + n for n in payloads}
        for name, expected in payloads.items():
            assert archive.read(PREFIX + name) == expected
            assert 'voice-config.json' not in name
            assert not name.endswith(('.bin', '.exe', '.log'))
        for name in FILES:
            if name.endswith('.command'):
                assert (archive.getinfo(PREFIX + name).external_attr >> 16) & 0o111 == 0o111
    print(output)
    print(f'{output.stat().st_size} bytes; SHA256 {hashlib.sha256(output.read_bytes()).hexdigest()}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    build(parser.parse_args().output.resolve())
