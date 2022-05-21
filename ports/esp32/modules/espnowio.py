# espnowio module for MicroPython on ESP32
# MIT license; Copyright (c) 2022 Glenn Moloney @glenn20

from esp import espnow

ETH_ALEN = espnow.ETH_ALEN
MAX_DATA_LEN = espnow.MAX_DATA_LEN
KEY_LEN = espnow.KEY_LEN
MAX_TOTAL_PEER_NUM = espnow.MAX_TOTAL_PEER_NUM
MAX_ENCRYPT_PEER_NUM = espnow.MAX_ENCRYPT_PEER_NUM


class ESPNow(ESPNow):
    # Static buffers for alloc free receipt of messages with ESPNow.irecv().
    _buffers = [None, bytearray(MAX_DATA_LEN)]
    _none_tuple = (None, None)

    def __init__(self):
        super().__init__()

    def irecv(self, timeout=None):
        n = self.recvinto(self._buffers, timeout)
        return self._buffers if n > 0 else self._none_tuple

    def recv(self, timeout=None):
        n = self.recvinto(self._buffers, timeout)
        return [bytes(x) for x in self._buffers] if n else self._none_tuple

    def irq(recv_cb):
        def cb_wrapper(e):
            data = e.irecv(0)
            recv_cb(ESPNOW_EVENT_DATA, data)

        super().irq(cb_wrapper)

    def __iter__(self):
        return self

    def __next__(self):
        return self.irecv()  # Use alloc free irecv() method

    # Backward compatibility with pre-release API
    def init(self, *args):
        keys = "rxbuf, timeout"
        kwargs = {keys[i]: args[i] for i in range(len(args))}
        if kwargs:
            self.config(**kwargs)
        self.active(True)

    def deinit(self):
        self.active(False)


# Convenience function to support:
#   import espnowio as espnow; e = espnow.ESPNow()
def ESPNow():
    return ESPNowIO()
