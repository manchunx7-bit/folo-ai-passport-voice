#!/usr/bin/env python3
"""Verify ADPCM -> forwarder -> VB-Cable -> recording endpoint without hotkeys."""

import array
import math
import os
import socket
import sys
import threading
import time

import sounddevice as sd

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(__file__), "..")))
import adpcm  # noqa: E402

RATE = 16000
BLOCK = 1600
TARGET = ("127.0.0.1", 33333)


def find_input():
    for index, device in enumerate(sd.query_devices()):
        if device["max_input_channels"] <= 0 or "CABLE Output" not in device["name"]:
            continue
        try:
            sd.check_input_settings(device=index, samplerate=RATE, channels=1,
                                    dtype="int16")
            return index, device["name"]
        except sd.PortAudioError:
            continue
    raise RuntimeError("No 16 kHz CABLE Output recording endpoint")


def send_tone():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    state = adpcm.AdpcmState()
    time.sleep(0.25)
    for seq in range(12):
        pcm = [int(10000 * math.sin(2 * math.pi * 440 * (seq * BLOCK + i) / RATE))
               for i in range(BLOCK)]
        frame = bytes((0x01, seq & 0xFF, 0x80)) + adpcm.encode_block(state, pcm)
        sock.sendto(frame, TARGET)
        time.sleep(0.1)
    sock.close()


def main():
    index, name = find_input()
    samples = array.array("h")
    sender = threading.Thread(target=send_tone, daemon=True)
    with sd.RawInputStream(samplerate=RATE, channels=1, dtype="int16",
                           device=index, blocksize=BLOCK) as stream:
        sender.start()
        for _ in range(18):
            data, _overflow = stream.read(BLOCK)
            samples.frombytes(bytes(data))
    sender.join(timeout=2)
    peak = max((abs(v) for v in samples), default=0)
    rms = math.sqrt(sum(v * v for v in samples) / max(1, len(samples)))
    print(f"CABLE Output={name!r} samples={len(samples)} peak={peak} rms={rms:.1f}")
    if peak < 1000 or rms < 200:
        print("FAIL: VB-Cable did not carry the forwarded test tone", file=sys.stderr)
        return 1
    print("PASS: hardware audio decode and VB-Cable microphone path are working")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
