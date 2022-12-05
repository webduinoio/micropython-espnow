import time
import machine

pin = machine.Pin(18, machine.Pin.OUT)
pin.value(1)

# If we have just woken from a deep sleep go back to sleep
if machine.reset_cause() == machine.DEEPSLEEP_RESET:
    time.sleep_ms(1)
    pin.value(0)
    machine.deepsleep(1000)
