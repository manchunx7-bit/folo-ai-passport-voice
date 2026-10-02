import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hotkey_forwarder as h


class PortableReleaseTests(unittest.TestCase):
    def test_executable_adjacent_config_overrides_legacy(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'voice-config.json').write_text(json.dumps({'hotkey_key': 'left-shift'}))
            (root / 'config.local.json').write_text(json.dumps({'hotkey_key': 'f9'}))
            with mock.patch.object(h.sys, 'frozen', True, create=True), \
                 mock.patch.object(h.sys, 'executable', str(root / 'voice.exe')):
                self.assertEqual(h.load_local_config()['hotkey_key'], 'left-shift')

    def test_missing_config_has_safe_defaults(self):
        with tempfile.TemporaryDirectory() as temp, \
             mock.patch.object(h.sys, 'frozen', True, create=True), \
             mock.patch.object(h.sys, 'executable', str(Path(temp) / 'voice.exe')):
            self.assertEqual(h.load_local_config(), {})

    def test_failed_endpoint_is_closed_before_fallback(self):
        first, second = mock.Mock(), mock.Mock()
        first.start.side_effect = RuntimeError('endpoint unsupported')
        sd = types.SimpleNamespace(
            query_devices=lambda: [dict(name='CABLE Input', max_output_channels=2)] * 2,
            RawOutputStream=mock.Mock(side_effect=[first, second]))
        with mock.patch.dict(sys.modules, {'sounddevice': sd}), \
             mock.patch.object(h.threading, 'Thread'):
            sink = h.AudioSink('CABLE Input')
        self.assertTrue(sink.enabled)
        self.assertIs(sink._stream, second)
        first.close.assert_called_once()
        self.assertEqual(sd.RawOutputStream.call_args_list[1].kwargs['device'], 1)

    def test_all_failed_endpoints_do_not_report_ready(self):
        sd = types.SimpleNamespace(
            query_devices=lambda: [dict(name='CABLE Input', max_output_channels=2)],
            RawOutputStream=mock.Mock(side_effect=RuntimeError('unavailable')))
        with mock.patch.dict(sys.modules, {'sounddevice': sd}), \
             mock.patch.object(h.threading, 'Thread') as thread:
            sink = h.AudioSink('CABLE Input')
        self.assertFalse(sink.enabled)
        thread.assert_not_called()


if __name__ == '__main__':
    unittest.main()
