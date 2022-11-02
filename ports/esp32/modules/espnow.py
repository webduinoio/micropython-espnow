# espnow module for MicroPython on ESP32
# MIT license; Copyright (c) 2022 Glenn Moloney @glenn20

from micropython import const
import _espnow

EVENT_RECV_MSG = const(1)


# A dict-like interface to the peers register
class Peers:
    def __delitem__(self, peer):
        _espnow.del_peer(peer)

    def __getitem__(self, peer):
        return _espnow.get_peer(peer)

    def __setitem__(self, peer, value):
        try:
            _espnow.del_peer(peer)
        except OSError:
            pass
        _espnow.add_peer(peer, *value)

    def __len__(self):
        return len(_espnow.get_peers())

    def items():
        return ((p[0], p) for p in _espnow.get_peers())

    def keys():
        return (p[0] for p in _espnow.get_peers())

    def values():
        return (p for p in _espnow.get_peers())


class ESPNow:
    # Static buffers for alloc free receipt of messages with ESPNow.irecv().
    _data = [None, bytearray(_espnow.MAX_DATA_LEN)]
    _none_tuple = (None, None)

    peers = Peers()

    # Convenience methods
    @classmethod
    def irecv(cls, timeout=None):
        n = _espnow.recvinto(cls._data, timeout)
        return cls._data if n else cls._none_tuple

    @classmethod
    def recv(cls, timeout=None):
        n = _espnow.recvinto(cls._data, timeout)
        return [bytes(x) for x in cls._data] if n else cls._none_tuple

    def irq(self, recv_cb):  # irq() api
        def cb_wrapper(e):
            recv_cb(EVENT_RECV_MSG, e.irecv(0))

        _espnow.on_recv(cb_wrapper, self)

    def __iter__(self):
        return self

    def __next__(self):
        return self.irecv()  # Use alloc free irecv() method

    # Backward compatibility with pre-release API
    @staticmethod
    def init():
        _espnow.active(True)

    @staticmethod
    def deinit():
        _espnow.active(False)

    # Present _espnow.* functions as methods
    @staticmethod
    def active(*args):
        return _espnow.active(*args)

    @staticmethod
    def config(*args, **kwargs):
        return _espnow.config(*args, **kwargs)

    def on_recv(self, recv_cb, arg=None):
        _espnow.on_recv(recv_cb, self if arg is None else arg)

    @staticmethod
    def stats():
        return _espnow.stats()

    @staticmethod
    def recvinto(buffers, timeout_ms=None):
        return _espnow.recvinto(buffers, timeout_ms)

    @staticmethod
    def send(mac, msg=None, sync=None):
        if msg is None:
            msg, mac = mac, None  # If msg is None: swap mac and msg
        return _espnow.send(mac, msg, sync)

    @staticmethod
    def any():
        return _espnow.any()

    @staticmethod
    def set_pmk(key):
        return _espnow.set_pmk(key)

    @staticmethod
    def add_peer(peer, *args, **kwargs):
        return _espnow.add_peer(peer, *args, **kwargs)

    @staticmethod
    def del_peer(peer):
        _espnow.del_peer(peer)

    @staticmethod
    def get_peers():
        return _espnow.get_peers()

    @staticmethod
    def get_peer(peer):
        try:
            return _espnow.get_peer(peer)
        except AttributeError:
            return next((p for p in _espnow.get_peers() if p[0] == peer), None)

    @staticmethod
    def mod_peer(peer, *args, **kwargs):
        try:
            _espnow.mod_peer(peer, *args, **kwargs)
        except AttributeError:
            _espnow.del_peer(peer)
            _espnow.add_peer(peer, *args, **kwargs)

    @staticmethod
    def peer_count():
        try:
            return _espnow.peer_count()
        except AttributeError:
            return len(_espnow.get_peers())


from _espnow import *
