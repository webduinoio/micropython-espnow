# Test of a ESPnow echo server and client transferring data.
# Test the ESP32 extemnsions. Assumes instance1 is an ESP32.
# Instance1 may be and ESP32 or ESP8266

try:
    import network
    import random
    import uselect
    import usys
    import espnow
except ImportError:
    print("SKIP")
    raise SystemExit

# Set read timeout to 5 seconds
timeout = 5000
default_pmk = b"Micropyth0nRules"
sync = True


def echo_server(e):
    peers = []
    while True:
        peer, msg = e.irecv(timeout)
        if peer is None:
            return
        if peer not in peers:
            peers.append(peer)
            e.add_peer(peer)

        #  Echo the MAC and message back to the sender
        if not e.send(peer, msg, sync):
            print("ERROR: send() failed to", peer)
            return

        if msg == b"!done":
            return


def client_send(e, peer, msg, sync):
    print("TEST: send/recv(msglen=", len(msg), ",sync=", sync, "): ", end="", sep="")
    try:
        if not e.send(peer, msg, sync):
            print("ERROR: Send failed.")
            return
    except OSError as exc:
        # Don't print exc as it is differs for esp32 and esp8266
        print("ERROR: OSError:")
        return


def init(sta_active=True, ap_active=False):
    wlans = [network.WLAN(i) for i in [network.STA_IF, network.AP_IF]]
    e = espnow.ESPNow()
    e.active(True)
    e.set_pmk(default_pmk)
    wlans[0].active(sta_active)
    wlans[1].active(ap_active)
    wlans[0].disconnect()  # Force esp8266 STA interface to disconnect from AP
    return e


# Server
def instance0():
    e = init(True, False)
    multitest.globals(PEERS=[network.WLAN(i).config("mac") for i in (0, 1)])
    multitest.next()
    print("Server Start")
    echo_server(e)
    print("Server Done")
    e.active(False)


# Client
def instance1():
    # Instance 1 (the client)
    e = init(True, False)
    e.config(timeout=timeout)
    multitest.next()
    peer = PEERS[0]
    e.add_peer(peer)

    print("RECVINTO() test...")
    msg = bytes([random.getrandbits(8) for _ in range(12)])
    client_send(e, peer, msg, True)
    data = [bytearray(espnow.ADDR_LEN), bytearray(espnow.MAX_DATA_LEN)]
    n = e.recvinto(data)
    print("OK" if data[1] == msg else "ERROR: Received != Sent")

    print("IRECV() test...")
    msg = bytes([random.getrandbits(8) for _ in range(12)])
    client_send(e, peer, msg, True)
    p2, msg2 = e.irecv()
    print("OK" if msg2 == msg else "ERROR: Received != Sent")

    print("RECV() test...")
    msg = bytes([random.getrandbits(8) for _ in range(12)])
    client_send(e, peer, msg, True)
    p2, msg2 = e.recv()
    print("OK" if msg2 == msg else "ERROR: Received != Sent")

    print("ITERATOR() test...")
    msg = bytes([random.getrandbits(8) for _ in range(12)])
    client_send(e, peer, msg, True)
    p2, msg2 = next(e)
    print("OK" if msg2 == msg else "ERROR: Received != Sent")

    # Tell the server to stop
    print("DONE")
    msg = b"!done"
    client_send(e, peer, msg, True)
    p2, msg2 = e.irecv()
    print("OK" if msg2 == msg else "ERROR: Received != Sent")

    e.active(False)