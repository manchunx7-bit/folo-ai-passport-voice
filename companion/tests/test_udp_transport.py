#!/usr/bin/env python3
"""UdpTransport 测试: 协议常量、报文路由、判活、以及与 relay 重组器的契约。

不依赖真实设备:大部分用例直接喂 `_on_datagram`(用一个"立即执行"的假事件循环),
最后一组用 127.0.0.1 上的真 socket 做一遍完整往返。

契约来源:passport-os/docs/voice-udp-channel.md(与固件 udp_audio.c 同构)。
"""
import asyncio
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from udp_transport import (T_AUDIO, T_BEACON, T_EVENT, T_PING, UDP_PORT,
                           UdpError, UdpTransport)


class FakeLoop:
    """把 call_soon_threadsafe 变成同步直调(测试里不需要真线程)。"""

    def __init__(self):
        self.calls = []

    def call_soon_threadsafe(self, fn, *args):
        self.calls.append(fn)
        fn(*args)


def _mk(bind_host="0.0.0.0", **kw):
    t = UdpTransport(bind_host=bind_host, **kw)
    t._loop = FakeLoop()
    t._evt_handler = lambda d: None
    t._aud_handler = lambda d: None
    return t


# ---- 协议常量(与固件 udp_audio.h 同值,改一侧必须同步) ----

def test_protocol_constants_frozen():
    assert UDP_PORT == 33333
    assert (T_AUDIO, T_EVENT, T_BEACON, T_PING) == (0x01, 0x02, 0x03, 0x04)


# ---- 报文路由 ----

def test_beacon_is_ignored():
    t = _mk()
    t._on_datagram(None, bytes([T_BEACON]) + b'{"v":1,"role":"pc"}',
                   ("192.168.1.9", UDP_PORT))
    assert t._device is None          # 自己广播的回环不得被当成设备


def test_ping_refreshes_device_but_does_not_dispatch():
    t = _mk()
    seen = []
    t._evt_handler = seen.append
    t._aud_handler = seen.append
    t._on_datagram(None, bytes([T_PING]), ("192.168.1.50", UDP_PORT))
    assert t._device == ("192.168.1.50", UDP_PORT)
    assert seen == []                 # 判活报文不派发给 relay


def test_audio_datagram_routes_to_audio_handler():
    t = _mk()
    got = []
    t._aud_handler = got.append
    payload = bytes([7, 0x80]) + bytes(804)
    t._on_datagram(None, bytes([T_AUDIO]) + payload, ("192.168.1.50", UDP_PORT))
    assert got == [payload]           # 逐字节透传(含 2B 分片帧头)
    assert t._device == ("192.168.1.50", UDP_PORT)


def test_event_datagram_routes_to_event_handler():
    t = _mk()
    got = []
    t._evt_handler = got.append
    payload = b'{"event":"voice.start","audio":"ima_adpcm"}\n'
    t._on_datagram(None, bytes([T_EVENT]) + payload, ("192.168.1.50", UDP_PORT))
    assert got == [payload]


def test_unknown_type_is_ignored():
    t = _mk()
    got = []
    t._evt_handler = got.append
    t._aud_handler = got.append
    t._on_datagram(None, bytes([0x7F]) + b"junk", ("192.168.1.50", UDP_PORT))
    assert got == []
    assert t._device is None          # 未知来源不建立链路


def test_type_only_datagram_does_not_crash():
    t = _mk()
    t._on_datagram(None, bytes([T_AUDIO]), ("192.168.1.50", UDP_PORT))
    # 空音频载荷透传下去由 relay 记 miss,传输层自己不能崩


# ---- 订阅前缓冲 ----

def test_pending_flushed_when_both_handlers_registered():
    async def run():
        t = UdpTransport()
        t._loop = FakeLoop()
        t._route_frame(T_EVENT, b'{"event":"device.hello"}\n')
        assert t._pending                    # 无 handler → 进缓冲
        got = []
        await t.start_notify("0000A2B2-0000-1000-8000-00805F9B34FB", got.append)
        await t.start_notify("0000A2B3-0000-1000-8000-00805F9B34FB", lambda d: None)
        assert got == [b'{"event":"device.hello"}\n']
        assert t._pending == []
    asyncio.run(run())


def test_unknown_uuid_rejected():
    async def run():
        t = UdpTransport()
        try:
            await t.start_notify("deadbeef", lambda d: None)
        except UdpError:
            return
        raise AssertionError("未知特征应被拒绝")
    asyncio.run(run())


# ---- 判活 ----

def test_check_link_times_out():
    t = UdpTransport(link_timeout=0.05)
    assert t._check_link() is False           # 还没见过设备
    t._device = ("192.168.1.50", UDP_PORT)
    t._device_last = time.monotonic()
    assert t._check_link() is False           # 刚收到过
    time.sleep(0.08)
    assert t._check_link() is True            # 静默超时 → 读线程退出


def test_write_without_device_raises():
    async def run():
        t = UdpTransport()
        try:
            await t.write_gatt_char("0000A2B1-0000-1000-8000-00805F9B34FB", b"{}")
        except UdpError:
            return
        raise AssertionError("设备未知时下行应抛 UdpError")
    asyncio.run(run())


# ---- 与 relay 重组器的契约(设备发出的 AUDIO 载荷必须能被直接解码) ----

def test_audio_payload_decodes_with_relay_reassembler():
    from adpcm import ADPCM_BLOCK_BYTES
    from relay import reassemble_adpcm

    block = bytes([0x00] * ADPCM_BLOCK_BYTES)   # 全零块:解码器应产出 1600 样本
    state = {"seq": None, "buf": bytearray(), "last_idx": -1,
             "last_seq": None, "miss": 0, "unit": None}
    for seq in (0, 1, 2, 255, 0):               # 含 mod 256 回绕
        payload = bytes([seq, 0x80]) + block
        state, frames = reassemble_adpcm(state, payload)
        assert len(frames) == 1
        assert len(frames[0]) == 3200            # 100ms @16kHz/16bit/mono
    assert state["miss"] == 0


# ---- 真 socket 往返(127.0.0.1) ----

def _free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def test_connect_discovers_device_then_receives_events():
    async def run():
        port = _free_port()
        t = UdpTransport(port=port, beacon_interval=30.0,
                         device_wait_timeout=5.0, link_timeout=5.0)
        assert await t.scan_for_device("AI Passport", 1)   # 立即返回占位地址

        dev = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        dev.bind(("127.0.0.1", 0))
        try:
            conn = asyncio.create_task(t.connect("udp://x", lambda: None))
            await asyncio.sleep(0.2)      # 让 connect 先把 socket 与读线程起来
            dev.sendto(bytes([T_PING]), ("127.0.0.1", port))
            await asyncio.wait_for(conn, timeout=5)
            assert t._device == ("127.0.0.1", dev.getsockname()[1])

            got = []
            await t.start_notify("0000A2B2-0000-1000-8000-00805F9B34FB", got.append)
            await t.start_notify("0000A2B3-0000-1000-8000-00805F9B34FB",
                                 lambda d: None)
            dev.sendto(bytes([T_EVENT]) + b'{"event":"voice.end"}\n',
                       ("127.0.0.1", port))
            for _ in range(40):           # 最多等 2s
                if got:
                    break
                await asyncio.sleep(0.05)
            assert got == [b'{"event":"voice.end"}\n']
        finally:
            await t.disconnect()
            dev.close()
    asyncio.run(run())
