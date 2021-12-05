# Test of a ESPnow echo server and client transferring data.
# Test the ESP32 extemnsions. Assumes instance1 is an ESP32.
# Instance1 may be and ESP32 or ESP8266

try:
    import network
    import random
    import usys
    from esp import espnow
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
    print("OK")


def init(sta_active=True, ap_active=False):
    wlans = [network.WLAN(i) for i in [network.STA_IF, network.AP_IF]]
    e = espnow.ESPNow()
    e.init()
    e.set_pmk(default_pmk)
    wlans[0].active(sta_active)
    wlans[1].active(ap_active)
    wlans[0].disconnect()   # Force esp8266 STA interface to disconnect from AP
    return e


def poll(e):
    poll = uselect.poll()
    poll.register(e, uselect.POLLIN)
    p = poll.ipoll(timeout)
    if not p:
        print("ERROR: poll() timeout waiting for response.")
    return p


# Server
def instance0():
    e = init(True, False)
    multitest.globals(PEERS=[network.WLAN(i).config("mac") for i in (0, 1)])
    multitest.next()
    print("Server Start")
    echo_server(e)
    print("Server Done")
    e.deinit()


def instance1():
    print("SKIP")


try:
    import uasyncio as asyncio

    async def client():
        e = init(True, False)
        e.config(timeout=timeout)
        peer = PEERS[0]
        e.add_peer(peer)
        multitest.next()

        s = asyncio.StreamReader(e)
        print("READ() test...")
        msgs = []
        for i in range(5):
            msgs.append(bytes([random.getrandbits(8) for _ in range(12)]))
            client_send(e, peer, msgs[i], True)

        msg2 = b''
        for i in range(5):
            msg2 += await s.read(-1)
            if len(msg2) == 100:
                break
            print("INFO: s.read() returns {} bytes.".format(len(msg2)))

        if len(msg2) != 100:
            print("INFO: s.read() returns {} bytes.".format(len(msg2)))

        for i in range(5):
            msg = msgs[i]

            if (len(msg2) < 20 or msg2[0] != 0x99 or
                len(msg2) < 8 + msg2[1] or msg2[8 : 8 + msg2[1]] != msg):
                print("ERROR: i={} recv_len={}".format(i, len(msg2))
                    + (" magic={}".format(msg2[0]) if len(msg2) > 0 else "")
                    + (" msg_len={}".format(msg2[1]) if len(msg2) > 1 else "")
                    + (" peer={}".format(msg2[2:8]) if len(msg2) > 8 else "")
                    + (" recv_msg={}".format(msg2[8:min(len(msg2),8+msg2[1])]) if len(msg2) > 8 else "")
                    + (" sent={}".format(msg)))
            else:
                print("OK")
            msg2 = msg2[8 + msg2[1] :]

        # Tell the server to stop
        print("DONE")
        msg = b"!done"
        client_send(e, peer, msg, True)
        msg2 = await s.read(-1)
        print(
            "OK" if msg2[0] == 0x99 and msg2[8 : 8 + msg2[1]] == msg else "ERROR: Received != Sent"
        )

        e.deinit()

    # Client
    def instance1():
        # Instance 1 (the client) must be an ESP32
        if usys.platform != "esp32":
            print("SKIP")
            raise SystemExit

        asyncio.run(client())

except:
    pass
