"""Release flasher: validate payloads, back up first, never erase-all.

No device access unless --execute is supplied. Requires esptool 5.4.0.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OFFSETS = {'bootloader.bin': 0, 'partition-table.bin': 0x8000, 'passport-os.bin': 0x10000}
LIMITS = {'bootloader.bin': 0x8000, 'partition-table.bin': 0x1000, 'passport-os.bin': 0x400000}
ENTRY = struct.Struct('<HBBII16sI')


def partitions(data):
    result = []
    for offset in range(0, min(len(data), 0xC00), ENTRY.size):
        row = ENTRY.unpack_from(data, offset)
        if row[0] != 0x50AA:
            break
        result.append(row[1:])
    if not result:
        raise ValueError('No valid ESP partition table')
    return result


def validate_payloads(root):
    manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
    for name, offset in OFFSETS.items():
        payload = (root / 'firmware' / name).read_bytes()
        expected = manifest['firmware'][name]
        if expected['offset'] != offset or not 0 < len(payload) <= LIMITS[name]:
            raise ValueError(f'Invalid image size/offset: {name}')
        if hashlib.sha256(payload).hexdigest() != expected['sha256']:
            raise ValueError(f'SHA256 mismatch: {name}. Download the complete matching release.')
        if name != 'partition-table.bin' and payload[0] != 0xE9:
            raise ValueError(f'Invalid ESP image: {name}')
    layout = partitions((root / 'firmware/partition-table.bin').read_bytes())
    regions = {row[4].split(b'\0', 1)[0].decode(): (row[2], row[3]) for row in layout}
    if regions.get('factory') != (0x10000, 0x400000) or regions.get('cardid') != (0x410000, 0x4000):
        raise ValueError('Unsupported app/cardid partition layout')
    for name, (start, size) in regions.items():
        if start < 0x8000 or size <= 0 or start + size > 0x700000:
            raise ValueError(f'Partition overlaps boot region/recovery: {name}')
    return layout


def write_args(mode, root):
    names = ['passport-os.bin'] if mode == 'upgrade' else list(OFFSETS)
    args = ['write-flash']
    for name in names:
        args.extend([hex(OFFSETS[name]), str(root / 'firmware' / name)])
    return args


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='Your device COM port, e.g. COM7')
    parser.add_argument('--mode', choices=['first', 'upgrade'], required=True)
    parser.add_argument('--baud', type=int, choices=[115200, 460800], default=460800)
    parser.add_argument('--execute', action='store_true', help='Actually back up and write the device')
    args = parser.parse_args()
    layout = validate_payloads(ROOT)
    base = [sys.executable, '-m', 'esptool', '--chip', 'esp32c3', '--port', args.port,
            '--baud', str(args.baud)]
    write = base + write_args(args.mode, ROOT)
    print('Target: FoloToy AI Passport / ESP32-C3 / 8 MB, matching peripherals only.')
    print('Mode:', args.mode)
    print('Plan: verify chip + 8MB, back up ALL 8MB, then:')
    print(subprocess.list2cmdline(write))
    print('No erase-all. Writes exclude cardid at 0x410000 and recovery at 0x700000.')
    if not args.execute:
        print('Preview only: no port opened, nothing changed. Add --execute to proceed.')
        return 0
    from importlib.metadata import version
    if version('esptool') != '5.4.0':
        raise ValueError('Use esptool==5.4.0 as tested for this package.')
    result = subprocess.run(base + ['flash-id'], check=True, text=True, encoding='utf-8',
                            errors='replace', stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(result.stdout)
    if not re.search(r'Detected flash size:\s*8\s*MB\b', result.stdout, re.I):
        raise ValueError('Cannot confirm 8 MB Flash; refusing to write.')
    backup_dir = ROOT / 'backups'
    backup_dir.mkdir(exist_ok=True)
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup = backup_dir / f'private-device-{stamp}.bin'
    subprocess.run(base + ['read-flash', '0x0', '0x800000', str(backup)], check=True)
    if not backup.is_file() or backup.stat().st_size != 0x800000:
        raise ValueError('Complete backup missing; refusing to write.')
    raw = backup.read_bytes()
    backup.with_suffix('.sha256').write_text(hashlib.sha256(raw).hexdigest() + '\n', encoding='ascii')
    print('Private backup:', backup, '(do not upload/share)')
    if args.mode == 'upgrade' and partitions(raw[0x8000:0x9000]) != layout:
        raise ValueError('Existing partition layout differs; backup saved, no write performed. Review first-install instructions.')
    subprocess.run(write, check=True)
    print('Write and esptool verification completed. Follow the phone/Wi-Fi setup tutorial.')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print('STOP:', error, file=sys.stderr)
        raise SystemExit(1)
