import hashlib
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('verify', Path(__file__).parents[1] / 'tools/verify_firmware.py')
v = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = v
spec.loader.exec_module(v)

def table():
    raw = b''.join(v.ENTRY.pack(0x50AA, p.kind, p.subtype, p.offset, p.size,
                               p.label.encode().ljust(16, b'\0'), 0) for p in (
        v.Partition(1, 2, 0x9000, 0x6000, 'nvs'),
        v.Partition(1, 1, 0xF000, 0x1000, 'phy_init'),
        v.Partition(1, 0x82, 0x420000, 0x200000, 'storage'),
        v.Partition(0, 0, 0x10000, v.APP_MAX_SIZE, 'factory'),
        v.Partition(1, 2, v.CARDID_OFFSET, v.CARDID_SIZE, 'cardid')))
    raw += b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(raw).digest()
    return raw.ljust(v.PARTITION_TABLE_SIZE, b'\xff')

class FirmwareTests(unittest.TestCase):
    def test_table(self):
        self.assertTrue(v.parse_partition_table(table())[1])
        with self.assertRaises(ValueError): v.parse_partition_table(table()[:500])
        damaged = bytearray(table()); damaged[10] ^= 1
        with self.assertRaises(ValueError): v.parse_partition_table(damaged)

    def test_committed_layout(self):
        partitions, _ = v.parse_partition_table(table())
        by_label = {p.label: (p.offset, p.size) for p in partitions}
        csv = (Path(__file__).parents[1] / 'partitions.csv').read_text(encoding='utf-8')
        for row in csv.splitlines():
            if not row.strip() or row.lstrip().startswith('#'): continue
            fields = [s.strip() for s in row.split(',')]
            self.assertEqual(by_label[fields[0]], (int(fields[3], 0), int(fields[4], 0)))

    def test_protected_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'passport-os.bin').write_bytes(b'\xe9' + b'\0' * 63)
            merged = bytearray(b'\xff' * (v.RECOVERY_OFFSET + v.RECOVERY_SIZE))
            merged[0x10000] = 0xe9
            merged[0x8000:0x8000 + v.PARTITION_TABLE_SIZE] = table()
            v.verify_protected_layout(merged, path)
            for offset in (v.CARDID_OFFSET, v.RECOVERY_OFFSET):
                merged[offset] = 0
                with self.assertRaises(ValueError): v.verify_protected_layout(merged, path)
                merged[offset] = 0xff

if __name__ == '__main__': unittest.main()
