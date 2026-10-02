#!/usr/bin/env python3
"""Wi-Fi UDP 通道传输层(UdpTransport): 与设备同一局域网, 不经蓝牙/串口。

与 relay.py 的 BleakTransport / SerialTransport 同 5 方法契约(传输层注入式,
relay 核心零改动): scan_for_device / connect / write_gatt_char / start_notify /
disconnect。

协议契约(与固件 main/apps/voice/udp_audio.c 同构, 见
passport-os/docs/voice-udp-channel.md —— 改一侧必须同步另一侧):

    端口 UDP 33333(双向同端口)
    报文 = [1B type][payload]
      0x01 AUDIO   设备→电脑  [seq][0x80] + 804B IMA ADPCM block  = 807B
      0x02 EVENT   双向       UTF-8 JSON 行(含结尾 '\\n')          ≤ 513B
      0x03 BEACON  电脑→设备  {"v":1,"role":"pc","port":33333}     ~40B
      0x04 PING    设备→电脑  空载荷, 每 2s 一发, 只用于判活

角色:
  - 本端(companion)= 服务端: bind 0.0.0.0:33333, 每 1s 向 255.255.255.255:33333
    广播 beacon, 设备听到后学到本端 IP 并主动上行 device.hello;
  - 设备 = 客户端: 也 bind 33333(为收 beacon), 学到对端后单播过来。
    本端由 recvfrom 的来源地址学出设备地址, 因此设备不需要做任何宣告。

与 BLE/USB 通道的差异点:
  - scan_for_device 立即返回占位地址: 本端不扫描, 由设备主动找上门;
  - connect(address) 打开 socket、起读线程与 beacon 协程, 然后等设备第一个
    报文(最多 device_wait_timeout 秒), 超时抛 UdpError;
  - AUDIO 载荷就是设备侧组装好的 [seq][0x80]+804B 块 —— relay 的
    reassemble_adpcm() 按"新 seq + is_last"终结解码, 一行都不用改;
  - 链路判活: 设备每 2s 一发 0x04 PING。连续 link_timeout 秒无任何设备报文
    → 判定断开, 触发 on_disconnect(对齐 BLE 断连语义);
  - 无 SYS 命令面(不定义 send_syscmd) → relay 走 CTRL/time.set 路径。
"""

import asyncio
import json
import re
import socket
import subprocess
import sys
import threading
import time

UDP_PORT = 33333

# 报文类型(与固件 udp_audio.h 的 VOICE_UDP_T_* 契约一致)
T_AUDIO = 0x01
T_EVENT = 0x02
T_BEACON = 0x03
T_PING = 0x04

# 契约占位(与 BLE/USB 通道同名, relay 按 uuid 分发 handler)
SERVICE_UUID = "0000A2B0-0000-1000-8000-00805F9B34FB"
CTRL_UUID = "0000A2B1-0000-1000-8000-00805F9B34FB"
EVENT_UUID = "0000A2B2-0000-1000-8000-00805F9B34FB"
AUDIO_UUID = "0000A2B3-0000-1000-8000-00805F9B34FB"

PENDING_MAX = 64              # 订阅前缓冲上限(device.hello 等秒级几条)
BEACON_INTERVAL = 1.0         # beacon 周期(与固件 VOICE_UDP_PEER_TIMEOUT_MS=3000 成 3:1)
DEVICE_WAIT_TIMEOUT = 60.0    # connect 等设备首个报文的超时
LINK_TIMEOUT = 12.0           # 设备静默多久判断开(放宽到 12s，抗 2.4G Wi-Fi 偶发抖动)
BEACON_REFRESH = 30.0         # beacon 目标(本机广播段)重算周期
RECV_TIMEOUT = 0.5            # 读线程 recvfrom 超时(用于周期判活)
RECV_BUF = 2048               # ≥ 807B(AUDIO) 且 ≥ 513B(EVENT), 留余量
RECV_MAX = 4096               # 超过视为协议违约, 丢弃


class UdpError(Exception):
    """Wi-Fi UDP 通道错误(端口占用/等不到设备/未连接/发送失败)。"""


def _local_broadcast_addrs(port):
    """本机所有私网 IPv4 的定向广播 + 受限广播。

    多网卡(网线+WiFi 同网段/代理 TUN)时,255.255.255.255 从哪个口出去由
    系统路由决定,可能发错口导致设备收不到 beacon(实测踩过)。对每个私网
    网段都发一份定向广播(x.y.z.255),哪个口能通走哪个。

    IP 枚举双通道并集:gethostbyname_ex 走系统 DNS,在 Mihomo/Clash TUN
    (fake-ip 劫持 hostname 解析)下会漏掉局域网 IP(2026-09-13 实测:该窗口
    beacon 目标只剩 TUN/WSL 广播段,设备永远 OFFLINE),所以再用 ipconfig
    的输出兜底。ipconfig 中英文输出都含 "IPv4" 字样,取行内最后一个点分四段。
    """
    addrs = {"255.255.255.255"}
    ips = set()
    try:
        _, _, dns_ips = socket.gethostbyname_ex(socket.gethostname())
        # 2026-09-13 修复:IPv4 点分四段只有 **3** 个点。原来写 == 4,条件恒为
        # 假 —— gethostbyname_ex 拿到的私网 IP 被全部丢弃,只剩 255.255.255.255,
        # 多网卡时经常发错网口,设备收不到 beacon → 一直"与电脑断开/正在重连"。
        ips.update(i for i in dns_ips if i.count(".") == 3)
    except Exception:
        pass
    if sys.platform == "win32":
        try:
            # ipconfig 输出是 ANSI 代码页(zh-CN 为 GBK),text=True 的 UTF-8
            # 解码会炸掉读线程 → 必须用 mbcs + errors=replace。
            out = subprocess.run(
                ["ipconfig"], capture_output=True, timeout=5,
                encoding="mbcs", errors="replace").stdout or ""
            for line in out.splitlines():
                if "IPv4" not in line:
                    continue
                found = re.findall(r"\b(\d{1,3}(?:\.\d{1,3}){3})\b", line)
                if found:
                    ips.add(found[-1])
        except Exception:
            pass
    for ip in ips:
        parts = ip.split(".")
        if len(parts) != 4:
            continue
        if (ip.startswith("192.168.") or ip.startswith("10.") or
                (ip.startswith("172.") and 16 <= int(parts[1]) <= 31)):
            addrs.add(".".join(parts[:3]) + ".255")
    return [(a, port) for a in sorted(addrs)]


class UdpTransport:
    """Wi-Fi UDP 传输层(单设备;v1 不自动重连, 对齐 BLE 语义)。

    port: UDP 端口, 缺省 33333(与固件 VOICE_UDP_PORT 一致)。
    """

    def __init__(self, port=None, bind_host="0.0.0.0",
                 beacon_interval=BEACON_INTERVAL,
                 device_wait_timeout=DEVICE_WAIT_TIMEOUT,
                 link_timeout=LINK_TIMEOUT,
                 auto_recover=True,
                 device_ip=None):
        self._port = int(port) if port else UDP_PORT
        self._bind_host = bind_host
        self._beacon_interval = beacon_interval
        self._device_wait_timeout = device_wait_timeout
        self._link_timeout = link_timeout
        self._device_ip = str(device_ip).strip() if device_ip else None
        # 2026-09-13 修复:UDP 是无连接的,"设备静默 6s"并不意味着链路要拆。
        # 原来静默超时会让读线程退出 → 回调 on_disconnect → relay 收束整个会话
        # 并关闭 socket;而设备侧会自动重连,结果电脑端必须手重启(WINDOWS.md
        # 记录的 v1 不对称缺陷)。现在默认只告警、不拆链,beacon 继续发,
        # 设备回来后 _note_device 自动刷新地址,会话原地恢复。
        self._auto_recover = bool(auto_recover)
        self._lost_logged = False
        self._sock = None
        self._reader = None
        self._beacon_task = None
        self._loop = None
        self._stop = False
        self._on_disconnect = None
        self._evt_handler = None
        self._aud_handler = None
        self._pending = []          # (type, payload) 订阅前缓冲
        self._device = None         # (ip, port) 由 recvfrom 学出
        self._device_last = 0.0     # 最近一次收到设备报文的单调时刻

    # -- 5 方法契约(对齐 BleakTransport) --

    async def scan_for_device(self, name, timeout):
        """本端是 UDP 服务端, 不扫描: 立即返回占位地址, 由 connect() 等设备上门。

        name/timeout 忽略(设备无广播宣告, 只有"设备听到 beacon 后主动 hello")。
        """
        print(f"[udp] 服务端模式: 监听 {self._bind_host}:{self._port}, "
              f"每 {self._beacon_interval:.0f}s 广播 beacon")
        return f"udp://{self._bind_host}:{self._port}"

    async def connect(self, address, on_disconnect=None):
        """开 socket → 起读线程与 beacon 协程 → 等设备第一个报文。

        等不到(device_wait_timeout 秒)抛 UdpError。address 忽略(占位)。
        """
        self._on_disconnect = on_disconnect
        self._loop = asyncio.get_running_loop()
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            # 2026-09-13 修复:Windows 上 SO_REUSEADDR 语义等同于 SO_REUSEPORT,
            # 允许第二个进程静默绑定同一端口并抢走设备报文(排查时极难定位)。
            # 仅在 POSIX 上保留(用于快速重启回收 TIME_WAIT)。
            if sys.platform != "win32":
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.bind((self._bind_host, self._port))
        except OSError as e:
            raise UdpError(
                f"绑定 UDP {self._bind_host}:{self._port} 失败: {e}\n"
                "(确认端口未被占用、已在防火墙放行入站 UDP)") from e
        self._sock = sock
        self._stop = False
        self._device = None
        self._reader = threading.Thread(target=self._read_loop,
                                        name="udp_reader", daemon=True)
        self._reader.start()
        self._beacon_task = asyncio.create_task(self._beacon_loop())
        print(f"[udp] 已监听 {self._bind_host}:{self._port}, "
              f"等待设备(请在设备上进入「AI语音」)...")
        deadline = time.monotonic() + self._device_wait_timeout
        while (self._device is None and not self._stop
               and time.monotonic() < deadline):
            await asyncio.sleep(0.05)
        if self._device is None:
            await self.disconnect()
            raise UdpError(
                f"{self._device_wait_timeout:.0f}s 内没有收到设备报文:"
                "设备未开机/未进入「AI语音」/不在同一局域网"
                "(路由器 AP 隔离会挡住 beacon)")
        print(f"[udp] 发现设备 {self._device[0]}:{self._device[1]}")

    async def write_gatt_char(self, uuid, data):
        """下行(EVENT 类型;uuid 忽略,UDP 无特征区分)。设备未知时抛 UdpError。"""
        if self._sock is None or self._device is None:
            raise UdpError("设备未连接,下行丢弃")
        if not self._sendto_device(T_EVENT, bytes(data)):
            raise UdpError("下行发送失败")

    async def start_notify(self, uuid, handler):
        """注册 EVENT/AUDIO handler;两个都注册后冲刷订阅前缓冲。"""
        if uuid == EVENT_UUID:
            self._evt_handler = handler
        elif uuid == AUDIO_UUID:
            self._aud_handler = handler
        else:
            raise UdpError(f"未知特征: {uuid}(UDP 通道仅 EVENT/AUDIO 两类)")
        if self._evt_handler is not None and self._aud_handler is not None:
            pending, self._pending = self._pending, []
            for ftype, payload in pending:
                self._dispatch_frame(ftype, payload)

    async def disconnect(self):
        """停 beacon、关 socket、收束读线程(不触发 on_disconnect)。幂等。"""
        self._stop = True
        if self._beacon_task is not None:
            self._beacon_task.cancel()
            try:
                await self._beacon_task
            except (asyncio.CancelledError, Exception):
                pass
            self._beacon_task = None
        if self._sock is not None:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None
        if self._reader is not None:
            deadline = time.monotonic() + 2.0
            while self._reader.is_alive() and time.monotonic() < deadline:
                await asyncio.sleep(0.02)
            self._reader = None
        self._device = None

    # -- 内部: 发送 --

    def _sendto_device(self, ftype, payload):
        """[type][payload] 单播给设备。成功返回 True。"""
        if self._sock is None or self._device is None:
            return False
        pkt = bytes((ftype,)) + bytes(payload)
        try:
            self._sock.sendto(pkt, self._device)
            return True
        except OSError as e:
            print(f"[udp] 发送失败: {e}", file=sys.stderr)
            return False

    async def _beacon_loop(self):
        """广播 beacon(设备据此学到本端 IP)。

        2026-09-13 修复:目标列表原来只在启动时算一次 —— 电脑换 Wi-Fi、开/关
        VPN(TUN)之后广播段就过期了,设备再也收不到。现在每 30s 重算一次,
        变化即打印。另外**设备未上线时用 1s 快发**,缩短"设备已进入 AI语音"
        到"电脑学到设备"之间的等待。
        """
        targets = _local_broadcast_addrs(self._port)
        if self._device_ip and (self._device_ip, self._port) not in targets:
            targets.append((self._device_ip, self._port))
        print(f"[udp] beacon 目标: {[t[0] for t in targets]}")
        last_refresh = time.monotonic()
        pkt = bytes((T_BEACON,)) + json.dumps(
            {"v": 1, "role": "pc", "port": self._port}).encode("utf-8")
        while not self._stop:
            now = time.monotonic()
            if now - last_refresh >= BEACON_REFRESH:
                fresh = _local_broadcast_addrs(self._port)
                if self._device_ip and (self._device_ip, self._port) not in fresh:
                    fresh.append((self._device_ip, self._port))
                last_refresh = now
                if fresh != targets:
                    print(f"[udp] 网络变化,beacon 目标更新: "
                          f"{[t[0] for t in fresh]}", file=sys.stderr)
                    targets = fresh
            all_targets = list(targets)
            if self._device and self._device not in all_targets:
                all_targets.append(self._device)
            for t_ip, t_port in all_targets:
                try:
                    self._sock.sendto(pkt, (t_ip, t_port))
                except OSError as e:
                    print(f"[udp] beacon 发送失败({t_ip}): {e}", file=sys.stderr)
            try:
                # 设备还没出现时快发,出现后回到常规周期
                gap = 1.0 if self._device is None else self._beacon_interval
                await asyncio.sleep(gap)
            except asyncio.CancelledError:
                return

    # -- 内部: 接收 --

    def _read_loop(self):
        """读线程: recvfrom → 按类型路由。退出路径触发 on_disconnect。

        退出路径(均对齐 BLE 断连回调):
          - socket 被 disconnect() 关闭(OSError, 此时 _stop 已置位, 不回调);
          - 设备静默超过 link_timeout(判活失败, 回调 on_disconnect)。
        """
        sock = self._sock
        sock.settimeout(RECV_TIMEOUT)
        try:
            while not self._stop:
                try:
                    data, addr = sock.recvfrom(RECV_MAX)
                except socket.timeout:
                    if self._check_link():
                        break
                    continue
                except OSError:
                    if not self._stop:
                        print("[udp] socket 读取异常", file=sys.stderr)
                    break
                if self._stop:
                    break
                if not data:
                    continue
                self._on_datagram(sock, data, addr)
        finally:
            if not self._stop and self._loop is not None:
                try:
                    self._loop.call_soon_threadsafe(self._notify_disconnect)
                except RuntimeError:
                    pass    # 事件循环已关闭(程序收束中)

    def _check_link(self):
        """判活: 设备静默过久时是否拆链。

        返回 True = 读线程退出并触发断开回调(旧行为,仅 auto_recover=False)。
        默认返回 False:只打印一次告警,继续监听等待设备回来。
        """
        if self._device is None:
            return False
        if time.monotonic() - self._device_last <= self._link_timeout:
            if self._lost_logged:      # 设备回来了,清掉告警标记
                self._lost_logged = False
                print("[udp] 设备已恢复", file=sys.stderr)
            return False
        if not self._auto_recover:
            print(f"[udp] 设备静默超过 {self._link_timeout:.0f}s,判定断开",
                  file=sys.stderr)
            return True
        if not self._lost_logged:
            self._lost_logged = True
            print(f"[udp] 设备静默超过 {self._link_timeout:.0f}s,"
                  "保持监听等待其重连(beacon 继续发送)", file=sys.stderr)
        return False

    def _on_datagram(self, sock, data, addr):
        """读线程上下文: 按报文类型路由(事件/音频经 call_soon_threadsafe 进事件循环)。"""
        ftype = data[0]
        if ftype == T_BEACON:
            return                       # 自己广播出去的回环
        if ftype == T_PING:
            self._note_device(addr)      # 判活,不派发
            return
        if ftype not in (T_AUDIO, T_EVENT):
            print(f"[udp] 未知报文类型 0x{ftype:02x} 来自 {addr[0]}, 忽略",
                  file=sys.stderr)
            return
        self._note_device(addr)
        payload = bytes(data[1:])
        self._loop.call_soon_threadsafe(self._route_frame, ftype, payload)

    def _note_device(self, addr):
        """记录/刷新设备地址。地址变化时打印(设备换 IP / 重连)。"""
        self._device_last = time.monotonic()
        if self._device != addr:
            if self._device is not None:
                print(f"[udp] 设备地址变化 {self._device} → {addr}",
                      file=sys.stderr)
            self._device = addr

    def _route_frame(self, ftype, payload):
        """事件循环线程: handler 齐备 → 分发;未齐 → 订阅前缓冲(有界)。"""
        if self._evt_handler is not None and self._aud_handler is not None:
            self._dispatch_frame(ftype, payload)
        elif len(self._pending) < PENDING_MAX:
            self._pending.append((ftype, payload))
        else:
            print("[udp] 订阅前消息过多, 丢弃", file=sys.stderr)

    def _dispatch_frame(self, ftype, payload):
        """事件循环线程: 分发到 handler(兼容同步/异步回调, 异常不扩散)。"""
        handler = (self._aud_handler if ftype == T_AUDIO
                   else self._evt_handler)
        if handler is None:
            return
        try:
            res = handler(payload)
            if asyncio.iscoroutine(res):
                asyncio.create_task(res).add_done_callback(
                    lambda t: (t.exception() is not None and
                               print(f"[udp] 回调协程异常: {t.exception()}",
                                     file=sys.stderr)))
        except Exception as e:
            print(f"[udp] 派发回调执行异常 (type={ftype}): {e}", file=sys.stderr)

    def _notify_disconnect(self):
        """事件循环线程: 断连回调(对齐 BleakTransport 直调语义)。"""
        if self._on_disconnect is None:
            return
        try:
            res = self._on_disconnect()
            if asyncio.iscoroutine(res):
                asyncio.create_task(res).add_done_callback(
                    lambda t: (t.exception() is not None and
                               print(f"[udp] 断连回调协程异常: {t.exception()}",
                                     file=sys.stderr)))
        except Exception as e:
            print(f"[udp] 断连回调执行异常: {e}", file=sys.stderr)
