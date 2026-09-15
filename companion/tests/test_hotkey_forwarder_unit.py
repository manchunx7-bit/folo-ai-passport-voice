#!/usr/bin/env python3
"""Pure dispatch tests: validate key mappings without injecting into the desktop."""

import argparse
import json
import os
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(__file__), "..")))
import hotkey_forwarder as h  # noqa: E402


class ForwarderDispatchTest(unittest.TestCase):
    def setUp(self):
        args = argparse.Namespace(
            key="right-shift",
            mode="tap",
            no_audio=True,
            output_substr="CABLE Input",
        )
        self.forwarder = h.Forwarder(args)

    @staticmethod
    def event(name, **fields):
        return (json.dumps({"event": name, **fields}) + "\n").encode("utf-8")

    def test_up_ptt_clicks_right_shift_at_start_and_again_at_release(self):
        sent = []
        with mock.patch.object(h, "_send_key", side_effect=lambda *a, **kw: sent.append((a, kw))), \
             mock.patch.object(h.time, "sleep"):
            self.forwarder.on_event(self.event("voice.start"))
            self.forwarder.on_event(self.event("voice.end"))

        self.assertEqual(len(sent), 4)
        self.assertEqual(sent[0][0][:3], h.KEY_MAP["right-shift"])
        self.assertFalse(sent[0][1]["up"])
        self.assertEqual(sent[1][0][:3], h.KEY_MAP["right-shift"])
        self.assertTrue(sent[1][1]["up"])
        self.assertEqual(sent[2][0][:3], h.KEY_MAP["right-shift"])
        self.assertFalse(sent[2][1]["up"])
        self.assertEqual(sent[3][0][:3], h.KEY_MAP["right-shift"])
        self.assertTrue(sent[3][1]["up"])

    def test_down_maps_to_single_enter_click(self):
        sent = []
        with mock.patch.object(h, "_send_key", side_effect=lambda *a, **kw: sent.append((a, kw))), \
             mock.patch.object(h.time, "sleep"):
            self.forwarder.on_event(self.event("key.action", action="enter"))

        self.assertEqual(len(sent), 2)
        self.assertEqual(sent[0][0][:3], h.KEY_MAP["enter"])
        self.assertFalse(sent[0][1]["up"])
        self.assertEqual(sent[1][0][:3], h.KEY_MAP["enter"])
        self.assertTrue(sent[1][1]["up"])

    def test_no_audio_mode_discards_device_audio(self):
        self.forwarder.on_audio(b"\x01\x80payload")


if __name__ == "__main__":
    unittest.main()
