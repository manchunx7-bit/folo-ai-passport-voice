"""16 kHz device mono -> two identical CoreAudio channels, with OS rate conversion."""
import queue
import threading
import sys


def stereo_pcm(data):
    if len(data) % 2:
        raise ValueError('PCM16 音频长度不完整')
    result = bytearray(len(data) * 2)
    result[0::4] = result[2::4] = data[0::2]
    result[1::4] = result[3::4] = data[1::2]
    return bytes(result)


def coreaudio_options(sd):
    return sd.CoreAudioSettings(change_device_parameters=False,
        fail_if_conversion_required=False, conversion_quality='max')


def make_audio_sink(shared, device_substr, sd=None, start_thread=True):
    # Inherit only protocol decoding, bounded input queue and final-syllable drain.
    class MacAudioSink(shared.AudioSink):
        def __init__(self):
            self.enabled = False
            self.device_name = None
            self.fault = ''
            self._q = queue.Queue(maxsize=5)
            self._closing = threading.Event()
            self._thread = None
            self._stream = None
            self._device_substr = device_substr
            self._sd_module = sd
            self._init_ok = False
            self._try_init(start_thread)
            if not self._init_ok:
                # 2026-10-26: 初始化失败不再永久失败——后台重连（系统切音频/AirPods插拔
                # 等导致 CoreAudio 设备列表瞬时不稳时，数秒后重试即可恢复）
                retry = threading.Thread(target=self._retry_loop, args=(start_thread,),
                                         name='mac_audio_retry', daemon=True)
                retry.start()

        def _try_init(self, start_thread):
            """尝试打开 BlackHole 输出流。成功置 _init_ok/enabled=True，失败打印 stderr。"""
            try:
                module = self._sd_module
                if module is None:
                    import sounddevice as module
                devices = module.query_devices()
                for index, device in enumerate(devices):
                    if device['max_output_channels'] < 2 or self._device_substr.lower() not in device['name'].lower():
                        continue
                    stream = None
                    try:
                        stream = module.RawOutputStream(samplerate=16000, channels=2,
                            dtype='int16', device=index, blocksize=0, extra_settings=coreaudio_options(module))
                        stream.start()
                        self._stream = stream
                        self.device_name = device['name']
                        break
                    except Exception as error:
                        if stream is not None:
                            stream.close()
                        self.fault = str(error)
                if self._stream is None:
                    raise RuntimeError(self.fault or f'没有可用的双声道输出：{self._device_substr}；请安装 BlackHole 2ch')
                self.fault = ''
                self.enabled = True
                self._init_ok = True
                if start_thread and (self._thread is None or not self._thread.is_alive()):
                    self._thread = threading.Thread(target=self._writer, name='mac_audio', daemon=True)
                    self._thread.start()
            except Exception as error:
                self.enabled = False
                self.fault = str(error)
                if self._stream is not None:
                    try:
                        self._stream.close()
                    except Exception:
                        pass
                    self._stream = None
                print(f'[audio] Mac 音频通道未就绪：{error}（每 3s 自动重试）', file=sys.stderr)

        def _retry_loop(self, start_thread):
            """初始化失败时的后台重连：每 3s 重试，直到成功或 close()。"""
            import time
            while not self._closing.is_set() and not self._init_ok:
                time.sleep(3)
                if self._closing.is_set() or self._init_ok:
                    return
                print('[audio] 重试连接 BlackHole…', file=sys.stderr)
                self._try_init(start_thread)

        def _writer(self):
            while not self._closing.is_set():
                queued = True
                try:
                    data = self._q.get(timeout=.2)
                except queue.Empty:
                    queued = False
                    data = shared.SILENCE
                try:
                    if not self._closing.is_set():
                        self._stream.write(stereo_pcm(data))
                except Exception as error:
                    self.enabled = False
                    self._init_ok = False
                    self.fault = str(error)
                    self.clear()
                    print(f'[audio] Mac 声卡写入失败：{error}（转入自动重连）', file=sys.stderr)
                    try:
                        if self._stream:
                            self._stream.close()
                    except Exception:
                        pass
                    self._stream = None
                    threading.Thread(target=self._retry_loop, args=(True,),
                                     name='mac_audio_retry', daemon=True).start()
                    return
                finally:
                    if queued:
                        self._q.task_done()

        def close(self):
            self._closing.set()
            self.enabled = False
            self.clear()
            if self._thread and self._thread.ident is not None:
                self._thread.join(timeout=.6)
                if self._thread.is_alive() and self._stream:
                    try:
                        self._stream.abort()
                    except Exception:
                        pass
                    self._thread.join(timeout=.6)
            if self._stream and (not self._thread or not self._thread.is_alive()):
                try:
                    self._stream.close()
                except Exception as error:
                    self.fault = str(error)
                finally:
                    self._stream = None
    return MacAudioSink()
