import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('flash',Path(__file__).with_name('flash.py'))
flash=importlib.util.module_from_spec(spec);spec.loader.exec_module(flash)


class FlashSafetyTests(unittest.TestCase):
    def test_first_install_never_covers_identity_or_recovery(self):
        args=flash.write_args('first',Path('/release'))
        self.assertEqual(args[1::2],['0x0','0x8000','0x10000'])
        self.assertNotIn('--erase-all',args)

    def test_upgrade_only_writes_app(self):
        args=flash.write_args('upgrade',Path('/release'))
        self.assertEqual(args,['write-flash','0x10000',str(Path('/release/firmware/passport-os.bin'))])

    def test_invalid_partition_rejected(self):
        with self.assertRaises(ValueError):flash.partitions(bytes(4096))

    def test_tampered_image_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/'firmware').mkdir()
            (root/'firmware/bootloader.bin').write_bytes(b'\xe9bad')
            (root/'manifest.json').write_text(json.dumps({'firmware':{
                'bootloader.bin':{'offset':0,'sha256':hashlib.sha256(b'\xe9good').hexdigest()}}}))
            with self.assertRaisesRegex(ValueError,'SHA256 mismatch'):flash.validate_payloads(root)


if __name__=='__main__':unittest.main()
