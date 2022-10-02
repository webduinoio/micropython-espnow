# espnow module for MicroPython on ESP32
# MIT license; Copyright (c) 2022 Glenn Moloney @glenn20

from micropython import const, ringbuffer
import array

from _espnow import *

EVENT_RECV_MSG = const(1)


class ESPNow(ESPNow):
    # Static buffers for alloc free receipt of messages with ESPNow.irecv().
    _hdr = array.array('B', b'\x00' * 7)
    _message = bytearray(MAX_DATA_LEN)
    _data = [bytearray(ETH_ALEN), None]
    _none_tuple = (None, None)

    def __init__(self):
        super().__init__()
        self.buffer = ringbuffer(2 * MAX_PACKET_LEN)

    def recvinto(self, data, timeout):
        b = self.buffer
        if timeout is not None:
            b.settimeout(timeout)
        n = b.readinto(self._hdr)
        if (n != 7):
            return 0 if n == 0 else -1
        len = self._hdr[1]
        if self._hdr[0] != MAGIC or len > 250:
            raise OSError("Invalid buffer")
        b.readinto(self._data[0])
        b.readinto(self._message, len)
        data[1] = self._message[:len]

    def irecv(self, timeout=None):
        n = self.recvinto(self._data, timeout)
        return self._data if n else self._none_tuple

    def recv(self, timeout=None):
        n = self.recvinto(self._data, timeout)
        return [bytes(x) for x in self._data] if n else self._none_tuple

    def on_recv(self, recv_cb, arg=None):
        super().on_recv(recv_cb, self if arg is None else arg)

    def irq(self, recv_cb):  # irq() api
        def cb_wrapper(e):
            recv_cb(EVENT_RECV_MSG, e.irecv(0))

        super().on_recv(cb_wrapper, self)

    def __iter__(self):
        return self

    def __next__(self):
        return self.irecv()  # Use alloc free irecv() method

    # Backward compatibility with pre-release API
    def init(self):
        self.active(True)

    def deinit(self):
        self.active(False)
