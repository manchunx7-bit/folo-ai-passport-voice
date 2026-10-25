"""Portable tests for the macOS architecture gate."""
import subprocess
import sys
import unittest
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import platform_support as support


class RuntimeTest(unittest.TestCase):
    def test_native_arm64_and_intel_are_supported(self):
        result = subprocess.CompletedProcess(args=[], returncode=0, stdout='0\n')
        with mock.patch.object(support.subprocess, 'run', return_value=result):
            self.assertTrue(support.runtime_supported('Darwin', 'arm64', (3, 11)))
            self.assertTrue(support.runtime_supported('Darwin', 'x86_64', (3, 11)))

    def test_rosetta_translation_is_rejected(self):
        result = subprocess.CompletedProcess(args=[], returncode=0, stdout='1\n')
        with mock.patch.object(support.subprocess, 'run', return_value=result):
            self.assertFalse(support.runtime_supported('Darwin', 'x86_64', (3, 11)))
            self.assertIn('Rosetta', support.runtime_error('Darwin', 'x86_64', (3, 11)))

    def test_non_macos_is_rejected_without_calling_sysctl(self):
        with mock.patch.object(support.subprocess, 'run') as run:
            self.assertFalse(support.runtime_supported('Windows', 'x86_64', sys.version_info))
        run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
