# espnow module for MicroPython on ESP32
# MIT license; Copyright (c) 2022 Glenn Moloney @glenn20

from micropython import const, ringbuffer
import array
import time

from _espnow import *

EVENT_RECV_MSG = const(1)
SEND_TIMEOUT_MS = const(2000)


class ESPNow(ESPNow):
    # Static buffers for alloc free receipt of messages with ESPNow.irecv().
    _hdr = array.array("B", b"\x00" * 7)
    _mac = bytearray(ETH_ALEN)
    _message = bytearray(MAX_DATA_LEN)
    _data = [None, None]
    _none_tuple = (None, None)
    _track_rssi = True
    peers_table = {}

    def __init__(self):
        super().__init__()
        self.config(buffer=ringbuffer(2 * MAX_PACKET_LEN))

    def recvinto(self, data, timeout):
        b = self.buffer
        if timeout is not None:
            b.settimeout(timeout)
        hdr = self._hdr
        n = b.readinto(hdr)
        if not n or n != 7:
            return 0 if not n else -1
        len = hdr[1]
        if hdr[0] != MAGIC or len > 250:
            raise OSError("Invalid buffer")
        mac, message = self._mac, self._message
        b.readinto(mac)
        b.readinto(message, len)
        if self._track_rssi:
            rssi = hdr[6]
            if (rssi & 0x80):  # Two's complement
                rssi = - ((~rssi) & 0x8f)
            mac = bytes(mac)
            x = self.peers_table.get(mac, None)
            if not x:
                x = list([0, 0])
                self.peers_table[mac] = x
            x[0], x[1] = rssi, time.ticks_ms()
        data[0], data[1] = mac, message[:len]
        return len

    def send(self, mac, msg=None, sync=None):
        if msg is None:
            msg, mac = mac, None  # If msg is None: swap mac and msg
        start = time.ticks_ms()
        while True:  # Keep trying to send if the espnow send buffers are full
            try:
                return self.send(mac, msg, sync)
            except OSError as err:
                if len(err.args) < 2 or err.args[2] != "ESP_ERR_ESPNOW_NO_MEM":
                    raise
            if time.ticks_ms() - start > SEND_TIMEOUT_MS:
                break

    def any(self):
        return self.buffer.any()

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
