# espnow module for MicroPython on ESP32
# MIT license; Copyright (c) 2022 Glenn Moloney @glenn20

from _espnow import *


class ESPNow(ESPNow):
    # Static buffers for alloc free receipt of messages with ESPNow.irecv().
    _data = [None, bytearray(MAX_DATA_LEN)]
    _none_tuple = (None, None)

    def __init__(self):
        super().__init__()

    def irecv(self, timeout=None):
        n = self.recvinto(self._data, timeout)
        return self._data if n else self._none_tuple

    def recv(self, timeout=None):
        n = self.recvinto(self._data, timeout)
        return [bytes(x) for x in self._data] if n else self._none_tuple

    def __iter__(self):
        return self

    def __next__(self):
        return self.irecv()  # Use alloc free irecv() method

    # Backward compatibility with pre-release API
    def init(self):
        self.active(True)

    def deinit(self):
        self.active(False)
