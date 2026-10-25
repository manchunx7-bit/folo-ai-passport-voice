"""Setup regressions without real audio, keys or system configuration changes."""
import argparse
import io
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import setup_doctor as d


class ConfigTest(unittest.TestCase):
    def test_missing_default_requires_personal_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            config, exists = d.read_config(Path(folder), 'Windows')
        self.assertFalse(exists)
        self.assertEqual(config['udp_port'], 33333)

    def test_explicit_missing_file_is_not_silently_replaced(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):
                d.read_config(Path(folder), 'Windows', str(Path(folder) / 'missing.json'))

    def test_invalid_values_do_not_fall_back_to_defaults(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'config.json'
            for value in ('{', '[]', '{"udp_port":true}', '{"udp_port":65536}',
                          '{"udp_port":"33333"}', '{"device_ip":42}', '{"output_substr":""}'):
                with self.subTest(value=value):
                    path.write_text(value, encoding='utf-8')
                    with self.assertRaises((ValueError, TypeError)):
                        d.read_config(Path(folder), 'Windows', str(path))

    def test_legacy_and_utf8_bom_are_supported(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'windows').mkdir()
            (root / 'windows/config.local.json').write_text('{"udp_port":34567}', encoding='utf-8-sig')
            config, exists = d.read_config(root, 'Windows')
        self.assertTrue(exists)
        self.assertEqual(config['udp_port'], 34567)


class ReportTest(unittest.TestCase):
    def test_malformed_json_reports_position_without_echoing_contents(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'config.json'
            path.write_text('{"private": "example-private-value",', encoding='utf-8')
            with mock.patch.object(d.socket, 'socket') as sock:
                report = d.collect(config_path=path, system='Windows')
        item = next(c for c in report['checks'] if c['id'] == 'config')
        self.assertEqual(item['detail']['reason'], 'invalid_json')
        self.assertNotIn('example-private-value', json.dumps(report))
        sock.assert_not_called()

    def test_mac_permission_denial_does_not_inject_keys(self):
        sys.path.insert(0, str(TOOLS.parent / 'macos'))
        import mac_keys
        keyboard = mock.Mock(ready=False)
        with mock.patch.object(d.platform, 'machine', return_value='arm64'), \
             mock.patch.object(d, 'mac_runtime_supported', return_value=True), \
             mock.patch.object(d.socket, 'socket', return_value=mock.MagicMock()), \
             mock.patch.object(d, 'audio_check', return_value=d.check('audio', 'PASS', 'mock')), \
             mock.patch.object(mac_keys, 'QuartzKeyboard', return_value=keyboard):
            report = d.collect(system='Darwin')
        self.assertEqual(next(c['status'] for c in report['checks']
                              if c['id'] == 'keyboard_permission'), 'FAIL')
        self.assertEqual(report['overall'], 'NEEDS_ATTENTION')
        self.assertEqual(keyboard.mock_calls, [])

    def test_native_intel_mac_is_supported(self):
        sys.path.insert(0, str(TOOLS.parent / 'macos'))
        import mac_keys
        import platform_support
        with mock.patch.object(d.platform, 'machine', return_value='x86_64'), \
             mock.patch.object(platform_support, 'running_under_rosetta', return_value=False), \
             mock.patch.object(mac_keys, 'QuartzKeyboard', return_value=mock.Mock(ready=True)), \
             mock.patch.object(d.socket, 'socket', return_value=mock.MagicMock()), \
             mock.patch.object(d, 'audio_check', return_value=d.check('audio', 'PASS', 'mock')):
            report = d.collect(system='Darwin')
        self.assertEqual(next(c['status'] for c in report['checks'] if c['id'] == 'runtime'), 'PASS')

    def test_rosetta_mac_skips_audio_and_network_checks(self):
        sys.path.insert(0, str(TOOLS.parent / 'macos'))
        import platform_support
        with mock.patch.object(d.platform, 'machine', return_value='x86_64'), \
             mock.patch.object(platform_support, 'running_under_rosetta', return_value=True), \
             mock.patch.object(d.socket, 'socket') as sock, \
             mock.patch.object(d, 'audio_check') as audio:
            report = d.collect(system='Darwin')
        self.assertEqual(next(c['status'] for c in report['checks'] if c['id'] == 'runtime'), 'FAIL')
        sock.assert_not_called()
        audio.assert_not_called()

    def test_custom_port_is_bound_and_no_runtime_stream_is_opened(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'config.json'
            path.write_text('{"udp_port":34567}', encoding='utf-8')
            sock = mock.MagicMock()
            with mock.patch.object(d.platform, 'machine', return_value='AMD64'), \
                 mock.patch.object(d.socket, 'socket', return_value=sock), \
                 mock.patch.object(d, 'audio_check', return_value=d.check('audio', 'PASS', 'mock')):
                report = d.collect(config_path=path, system='Windows')
        sock.__enter__.return_value.bind.assert_called_once_with(('0.0.0.0', 34567))
        self.assertEqual(report['overall'], 'READY_FOR_USER_TEST')
        self.assertEqual(report['actual_typing'], 'NOT_RUN')
        self.assertTrue(report['pending_user_actions'])

    def test_busy_port_is_failure_not_ready(self):
        with mock.patch.object(d.platform, 'machine', return_value='AMD64'), \
             mock.patch.object(d.socket, 'socket', side_effect=OSError('busy')), \
             mock.patch.object(d, 'audio_check', return_value=d.check('audio', 'PASS', 'mock')):
            report = d.collect(system='Windows')
        self.assertEqual(next(c['status'] for c in report['checks'] if c['id'] == 'udp_port'), 'FAIL')
        self.assertEqual(report['overall'], 'NEEDS_ATTENTION')

    def test_audio_requires_both_cable_directions(self):
        backend = types.SimpleNamespace(query_devices=lambda: [dict(name='CABLE Input',
            max_input_channels=0, max_output_channels=2)], check_output_settings=mock.Mock(),
            RawOutputStream=mock.Mock())
        item = d.audio_check(dict(output_substr='CABLE Input'), 'Windows', backend)
        self.assertEqual(item['status'], 'FAIL')
        backend.RawOutputStream.assert_not_called()

    def test_audio_dependency_failure_remains_json_with_nonzero_exit(self):
        failed = dict(overall='NEEDS_ATTENTION', checks=[], actual_typing='NOT_RUN')
        with mock.patch.object(d, 'collect', return_value=failed), \
             mock.patch('sys.stdout', new_callable=io.StringIO) as stream:
            code = d.main(['--json'])
        self.assertEqual(code, 1)
        self.assertEqual(json.loads(stream.getvalue()), failed)
        with mock.patch.object(d.importlib, 'import_module', side_effect=ImportError):
            self.assertEqual(d.audio_check({}, 'Windows')['status'], 'FAIL')

    def test_legacy_diagnosis_uses_effective_command_line_port(self):
        sys.path.insert(0, str(TOOLS.parent / 'windows'))
        import diagnose_voice
        import hotkey_forwarder
        args = argparse.Namespace(key='f9', mode='tap', port=34567, output_substr='CABLE Input')
        sock = mock.MagicMock()
        backend = types.SimpleNamespace(query_devices=lambda: [], check_output_settings=mock.Mock())
        with mock.patch.object(hotkey_forwarder, 'load_local_config', return_value={'udp_port':33333}), \
             mock.patch.object(diagnose_voice.socket, 'socket', return_value=sock), \
             mock.patch.dict(sys.modules, sounddevice=backend), \
             mock.patch('sys.stdout', new_callable=io.StringIO):
            diagnose_voice.main(args)
        sock.__enter__.return_value.bind.assert_called_once_with(('0.0.0.0', 34567))


if __name__ == '__main__':
    unittest.main()
