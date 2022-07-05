# espnowio module for MicroPython on ESP8266
# MIT license; Copyright (c) 2022 Glenn Moloney @glenn20

from _espnow import *
from uselect import poll, POLLIN

ETH_ALEN = const(6)
MAX_DATA_LEN = const(250)
KEY_LEN = const(16)
MAX_TOTAL_PEER_NUM = const(20)
MAX_ENCRYPT_PEER_NUM = const(6)

class ESPNow(ESPNow):
    # Static buffers for alloc free receipt of messages with
    # ESPNow.irecv().
    _buffers = [bytearray(ETH_ALEN), bytearray(MAX_DATA_LEN)]
    _none_tuple = (None, None)

    def __init__(self):
        super().__init__()
        self._poll = poll()  # For any() method below...
        self._poll.register(self, POLLIN)

    def irecv(self, timeout=None):
        n = self.recvinto(self._buffers, timeout)
        return self._buffers if n > 0 else self._none_tuple

    def recv(self, timeout=None):
        n = self.recvinto(self._buffers, timeout)
        return [bytes(x) for x in self._buffers] if n else self._none_tuple

    def __iter__(self):
        return self

    def __next__(self):
        return self.irecv()  # Use alloc free irecv() method

    def any(self):  # For the ESP8266 which does not have ESPNow.any()
        try:
            next(self._poll.ipoll(0))
            return True
        except StopIteration:
            return False

    # Backward compatibility with pre-release API
    def init(self, *args):
        keys = "rxbuf, timeout"
        kwargs = {keys[i]: args[i] for i in range(len(args))}
        if kwargs:
            self.config(**kwargs)
        self.active(True)

    def deinit(self):
        self.active(False)

def ESPNow():
    return ESPNowIO()
