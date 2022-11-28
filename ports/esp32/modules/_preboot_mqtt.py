import machine


def send_state():
    import time
    import network
    import umqtt.simple as mqtt
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
        sta.ifconfig(secret["ifconfig"].decode().split(" "))
    time.sleep_ms(1)
    sta.connect(secret["wifi_ssid"], secret["wifi_password"])
    while not sta.isconnected():
        pass
    try:
        mq = mqtt.MQTTClient(
            "esp32_test",
            secret["mqtt_server"],
            user=secret["mqtt_user"],
            password=secret["mqtt_password"],
        )
        mq.connect()
        mq.publish("test/test", "Sensor state")
        mq.disconnect()
    except mqtt.MQTTException as err:
        print(err)
    sta.disconnect()
    sta.active(False)


if machine.reset_cause() == machine.DEEPSLEEP_RESET:
    try:
        send_state()
        machine.deepsleep(1000)
    except OSError as err:
        print(err)
        print("Resuming normal boot sequence")
