"""Read-only bridge diagnostics. No keyboard injection, recording or process termination."""
import importlib.metadata
import json
from pathlib import Path
import socket
import sys


def main(args=None):
    print('AI Passport Voice diagnostics (Windows)')
    print('Python:', sys.version.split()[0])
    problems = 0
    if sys.platform != 'win32':
        print('[FAIL] This release supports Windows only.')
        problems += 1
    from hotkey_forwarder import load_local_config, resolve_key_spec
    config = load_local_config()
    try:
        resolve_key_spec(args.key if args else (config.get('hotkey_key') or 'right-shift'))
        assert (args.mode if args else config.get('hotkey_mode', 'hold')) in ('hold', 'tap')
        print('[OK] Shortcut syntax. Verify the same shortcut in your input method.')
    except (ValueError, AssertionError):
        print('[FAIL] Invalid hotkey_key or hotkey_mode in voice-config.json.')
        problems += 1
    try:
        port = args.port if args else int(config.get('udp_port', 33333))
        if not 1 <= port <= 65535:
            raise ValueError('Port out of range')
    except (ValueError, TypeError):
        print('[FAIL] Invalid UDP port in config.')
        return 1
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('0.0.0.0', port))
        print(f'[OK] UDP {port} available. Start only one bridge.')
    except OSError as e:
        print(f'[BUSY] UDP {port}:', e)
        print('If your bridge is running this is expected; otherwise close the OLD bridge.')
        print(f'PowerShell: Get-NetUDPEndpoint -LocalPort {port} | Select OwningProcess')
        problems += 1
    try:
        import sounddevice as sd
        print('sounddevice:', importlib.metadata.version('sounddevice'))
        devices = sd.query_devices()
        outputs = []
        inputs = []
        for i, device in enumerate(devices):
            name = device['name']
            if 'cable' not in name.lower():
                continue
            print(f'  [{i}] {name}: inputs={device["max_input_channels"]}, outputs={device["max_output_channels"]}')
            output_substr = args.output_substr if args else (config.get('output_substr') or 'CABLE Input')
            if device['max_output_channels'] and output_substr.lower() in name.lower():
                outputs.append(i)
            if device['max_input_channels'] and 'cable output' in name.lower():
                inputs.append(i)
        valid = False
        for i in outputs:
            try:
                sd.check_output_settings(device=i, samplerate=16000, channels=1, dtype='int16')
                valid = True
            except Exception as e:
                print(f'[INFO] Endpoint {i} cannot use 16 kHz mono: {e}')
        if not valid or not inputs:
            print('[FAIL] Install VB-CABLE from https://vb-audio.com/Cable/ and reboot.')
            problems += 1
        else:
            print('[OK] Output format supported; select CABLE Output in the input method.')
    except Exception as e:
        print('[FAIL] Audio dependency/device enumeration:', e)
        problems += 1
    print('This check does NOT prove device audio delivery or WeType recognition.')
    print('Use the recording and typing checks in docs/QUICKSTART.zh-CN.md.')
    return 1 if problems else 0


if __name__ == '__main__':
    raise SystemExit(main())
