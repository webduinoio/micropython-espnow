# espnowio module for MicroPython on ESP8266
# MIT license; Copyright (c) 2022 Glenn Moloney @glenn20

from esp import espnow
from micropython import const

ETH_ALEN = const(6)
MAX_DATA_LEN = const(250)
KEY_LEN = const(16)
MAX_TOTAL_PEER_NUM = const(20)
MAX_ENCRYPT_PEER_NUM = const(6)


class ESPNowIO(espnow.ESPNow):
    # Static buffers for alloc free receipt of messages with
    # ESPNow.irecv().
    _peer = bytearray(6)
    _msg = bytearray(MAX_DATA_LEN)
    _buffers = [_peer, _msg]

    def __init__(self):
        super().__init__()

    # Backward compatibility with pre-release API
    def init(self):
        self.active(True)

    # Backward compatibility with pre-release API
    def deinit(self):
        self.active(False)

    # Convenience API for alloc-free recv()
    def irecv(self, timeout=None):
        return self.recv(timeout, ESPNowIO._buffers)

    def __iter__(self):
        return self

    def __next__(self):
        return self.irecv()  # Use alloc free irecv() method


_espnowio = None  # A pseudo-singleton for ESPNowIO.

# Convenience function to support:
#   import espnowio as espnow; e = espnow.ESPNow()
def ESPNow():
    global _espnowio
    if _espnowio == None:
        _espnowio = ESPNowIO()
    return _espnowio
