import machine


def send_state(broker):
    import network
    from _espnow import ESPNow

    enow = ESPNow()
    enow.active(True)
    enow.add_peer(broker)
    sta = network.WLAN(network.STA_IF)
    sta.active(True)
    enow.send(broker, b"wake_up", True)
    enow.active(False)
    sta.active(False)


if machine.reset_cause() == machine.DEEPSLEEP_RESET:
    # send_state(b"\xccP\xe3\xb5\x95\xac")
    send_state(b"\xf4\x12\xfaA\xf7T")
    machine.deepsleep(1000)
