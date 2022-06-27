# espnowio module for MicroPython on ESP32
# MIT license; Copyright (c) 2022 Glenn Moloney @glenn20

from esp import espnow

ETH_ALEN = espnow.ETH_ALEN
MAX_DATA_LEN = espnow.MAX_DATA_LEN
KEY_LEN = espnow.KEY_LEN
MAX_TOTAL_PEER_NUM = espnow.MAX_TOTAL_PEER_NUM
MAX_ENCRYPT_PEER_NUM = espnow.MAX_ENCRYPT_PEER_NUM


class ESPNowIO(espnow.ESPNow):
    # Static buffers for alloc free receipt of messages with
    # ESPNow.irecv().
    _peer = None  # Storage array for peer is not needed on ESP32
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
