import machine

if machine.reset_cause() == machine.DEEPSLEEP_RESET:
    machine.deepsleep(1000)
