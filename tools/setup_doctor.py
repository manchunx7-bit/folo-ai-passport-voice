#!/usr/bin/env python3
"""Local-only setup checks; no key injection, recording or configuration writes."""
import argparse
from datetime import datetime, timezone
import importlib
import ipaddress
import json
from pathlib import Path
import platform
import socket
import sys

ROOT = Path(__file__).resolve().parents[1]


def mac_runtime_supported(system, architecture):
    if system != 'Darwin':
        return False
    macos_path = str(ROOT / 'macos')
    if macos_path not in sys.path:
        sys.path.insert(0, macos_path)
    try:
        from platform_support import runtime_supported
    except ImportError:
        return False
    return runtime_supported(system=system, architecture=architecture)


def check(name, status, detail, next_action=''):
    return dict(id=name, status=status, detail=detail, next_action=next_action)


def read_config(root, system, explicit=None):
    folder = root / ('macos' if system == 'Darwin' else 'windows')
    path = Path(explicit) if explicit else folder / 'voice-config.json'
    if not explicit and not path.exists() and system == 'Windows':
        legacy = folder / 'config.local.json'
        if legacy.exists():
            path = legacy
    exists = path.exists()
    config = json.loads(path.read_text(encoding='utf-8-sig')) if exists else {}
    if explicit and not exists:
        raise ValueError('The explicitly selected config file does not exist.')
    if not isinstance(config, dict):
        raise ValueError('Config must be a JSON object.')
    defaults = dict(hotkey_key='f9' if system == 'Darwin' else 'right-shift',
                    hotkey_mode='hold', output_substr='BlackHole 2ch' if system == 'Darwin'
                    else 'CABLE Input', udp_port=33333, device_ip='')
    defaults.update(config)
    if defaults['hotkey_mode'] not in ('hold', 'tap'):
        raise ValueError('hotkey_mode must be hold or tap.')
    port = defaults['udp_port']
    if type(port) is not int or not 1 <= port <= 65535:
        raise ValueError('udp_port must be an integer from 1 to 65535.')
    for field in ('hotkey_key', 'output_substr'):
        if not isinstance(defaults[field], str) or not defaults[field].strip():
            raise ValueError(field + ' must be a non-empty string.')
    if not isinstance(defaults['device_ip'], str):
        raise ValueError('device_ip must be a string.')
    if defaults['device_ip']:
        try:
            ipaddress.IPv4Address(defaults['device_ip'])
        except ValueError:
            raise ValueError('device_ip must be empty or an IPv4 address.') from None
    return defaults, exists


def audio_check(config, system, sd=None):
    try:
        sd = sd or importlib.import_module('sounddevice')
        devices = list(sd.query_devices())
        candidates = [(i, d) for i, d in enumerate(devices)
                      if config['output_substr'].lower() in d['name'].lower()
                      and d['max_output_channels'] >= (2 if system == 'Darwin' else 1)]
        valid = []
        for index, device in candidates:
            if system == 'Darwin' and device['max_input_channels'] < 2:
                continue
            try:
                options = dict(device=index, samplerate=16000, dtype='int16',
                               channels=2 if system == 'Darwin' else 1)
                if system == 'Darwin':
                    options['extra_settings'] = sd.CoreAudioSettings(change_device_parameters=False,
                        fail_if_conversion_required=False, conversion_quality='max')
                sd.check_output_settings(**options)
                valid.append(index)
            except Exception:
                continue
        inputs = [i for i, d in enumerate(devices) if d['max_input_channels'] > 0
                  and 'cable output' in d['name'].lower()]
        if not valid or (system == 'Windows' and not inputs):
            return check('audio', 'FAIL', 'Virtual audio endpoints or 16 kHz format unavailable.',
                         'Check the platform guide, driver installation and selected endpoint.')
        return check('audio', 'PASS', dict(output_indices=valid,
                     input_indices=inputs if system == 'Windows' else valid),
                     'Confirm the input method uses this virtual microphone; test real typing.')
    except Exception as error:
        return check('audio', 'FAIL', 'Audio backend unavailable: ' + type(error).__name__,
                     'Use the platform guide to install/check sounddevice and the virtual driver.')


def collect(root=ROOT, config_path=None, system=None):
    system = system or platform.system()
    checks = []
    architecture = platform.machine()
    supported = (system == 'Windows' and sys.maxsize > 2 ** 32
                 and architecture.lower() in ('amd64', 'x86_64')) or (
                     mac_runtime_supported(system, architecture))
    checks.append(check('runtime', 'PASS' if supported and sys.version_info >= (3, 11) else 'FAIL',
                        dict(os=system, architecture=platform.machine(),
                             python=platform.python_version()),
                        'Source client needs Python 3.11+ on Windows x64 or native macOS arm64/x86_64.'))
    try:
        config, exists = read_config(root, system, config_path)
        for folder in ('windows', 'macos'):
            path = str(root / folder)
            if path not in sys.path:
                sys.path.insert(0, path)
        resolver = importlib.import_module('mac_keys' if system == 'Darwin'
                                          else 'hotkey_forwarder').resolve_key_spec
        try:
            resolver(config['hotkey_key'])
        except ValueError:
            raise ValueError('hotkey_key is unsupported; use the client --list-keys command.') from None
        checks.append(check('config', 'PASS', dict(hotkey_key=config['hotkey_key'],
            hotkey_mode=config['hotkey_mode'], output_substr=config['output_substr'],
            udp_port=config['udp_port'], personal_config=exists)))
        if not exists:
            checks.append(check('personal_config', 'USER_ACTION', 'Using defaults; no personal config.',
                                'Confirm the real input-method shortcut, then create voice-config.json.'))
    except (OSError, ValueError, TypeError, ImportError) as error:
        if isinstance(error, json.JSONDecodeError):
            detail = dict(reason='invalid_json', line=error.lineno, column=error.colno)
        elif isinstance(error, (OSError, UnicodeError, ImportError, TypeError)):
            detail = dict(reason='config_or_module_unreadable', error=type(error).__name__)
        else:
            detail = str(error)
        checks.append(check('config', 'FAIL', detail,
                            'Back up the file, validate JSON and fields using the setup playbook.'))
        config = None
    if config and supported:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.bind(('0.0.0.0', config['udp_port']))
            checks.append(check('udp_port', 'PASS', dict(port=config['udp_port']),
                                'Port binding does not prove device connectivity.'))
        except OSError:
            checks.append(check('udp_port', 'FAIL', dict(port=config['udp_port'], reason='bind_failed'),
                                'Identify the owning process; close only your confirmed old bridge.'))
        checks.append(audio_check(config, system))
        if system == 'Darwin':
            try:
                keyboard = importlib.import_module('mac_keys').QuartzKeyboard()
                ready = keyboard.ready
            except Exception:
                ready = False
            checks.append(check('keyboard_permission', 'PASS' if ready else 'FAIL',
                                'Accessibility permission checked without injecting keys.',
                                'Authorize the actual launching host, reopen it and recheck if needed.'))
    else:
        for name in ('udp_port', 'audio'):
            checks.append(check(name, 'NOT_RUN', 'Requires valid config and supported runtime.'))
    pending = [dict(id='input_method', action='Confirm microphone and actual shortcut with physical keyboard.'),
               dict(id='device_link', action='Open the device app on the same LAN; confirm readiness.'),
               dict(id='typing', action='User must speak into the device in a blank document and test Enter.')]
    failed = any(c['status'] in ('FAIL', 'USER_ACTION') for c in checks)
    return dict(schema_version=1, application='voice',
                checked_at=datetime.now(timezone.utc).isoformat(),
                overall='NEEDS_ATTENTION' if failed else 'READY_FOR_USER_TEST',
                checks=checks, pending_user_actions=pending, actual_typing='NOT_RUN')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json', action='store_true', help='Emit one JSON report, including failures.')
    parser.add_argument('--config', help='Check this config instead of the current platform default.')
    args = parser.parse_args(argv)
    report = collect(config_path=args.config)
    if args.json:
        print(json.dumps(report, ensure_ascii=True, indent=2))
    else:
        for item in report['checks']:
            print('[{status}] {id}: {detail}'.format(**item))
            if item['next_action']:
                print('  Next:', item['next_action'])
        print(report['overall'], '- actual typing remains NOT_RUN.')
    return 1 if report['overall'] == 'NEEDS_ATTENTION' else 0


if __name__ == '__main__':
    raise SystemExit(main())
