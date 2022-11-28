import machine


def send_state():
    import time
    import network
    import urequests
    from _secrets import secret

    sta = network.WLAN(network.STA_IF)
    sta.active(True)
    time.sleep_ms(1)
    sta.disconnect()
    while sta.isconnected():
        pass
    time.sleep_ms(1)
    if secret["ifconfig"]:
        # Use static IP if provided
        sta.ifconfig(secret["ifconfig"].split(" "))
    time.sleep_ms(1)
    sta.connect(secret["wifi_ssid"], secret["wifi_password"])
    while not sta.isconnected():
        pass
    time.sleep_ms(1)
    try:
        r = urequests.post(secret["post_server"], data="hello")
    except OSError:
        pass
    sta.disconnect()
    sta.active(False)


if machine.reset_cause() == machine.DEEPSLEEP_RESET:
    send_state()
    machine.deepsleep(1000)
