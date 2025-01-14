:mod:`espnow` --- Support for the ESP-NOW protocol
==================================================

.. module:: espnow
    :synopsis: ESP-NOW wireless protocol support

This module provides an interface to the `ESP-NOW <https://www.espressif.com/
en/products/software/esp-now/overview>`_ protocol provided by Espressif on
ESP32 and ESP8266 devices (`API docs <https://docs.espressif.com/
projects/esp-idf/en/latest/api-reference/network/esp_now.html>`_).

Table of Contents:
------------------

    - `Introduction`_
    - `Configuration`_
    - `Sending and Receiving Data`_
    - `Peer Management`_
    - `Callback Methods`_
    - `Exceptions`_
    - `Constants`_
    - `Wifi Signal Strength (RSSI) - (ESP32 Only)`_
    - `Supporting asyncio`_
    - `Broadcast and Multicast`_
    - `ESPNow and Wifi Operation`_
    - `ESPNow and Sleep Modes`_

Introduction
------------

ESP-NOW is a connectionless wireless communication protocol supporting:

- Direct communication between up to 20 registered peers:

  - Without the need for a wireless access point (AP),

- Encrypted and unencrypted communication (up to 6 encrypted peers),

- Message sizes up to 250 bytes,

- Can operate alongside Wifi operation (:doc:`network.WLAN<network.WLAN>`) on
  ESP32 and ESP8266 devices.

It is especially useful for small IoT networks, latency sensitive or power
sensitive applications (such as battery operated devices) and for long-range
communication between devices (hundreds of metres).

This module also support tracking the Wifi signal strength (RSSI) of peer
devices.

.. note::
  This module is still under development and its classes, functions, methods
  and constants are subject to change. This module is not yet included in the
  main branch of micropython. Until such a time that the code is accepted into
  the micropython main branch, the following resources are available:
  `Source code
  <https://github.com/glenn20/micropython/tree/espnow-g20>`_ |
  `Pre-compiled images
  <https://github.com/glenn20/micropython-espnow-images>`_ |
  `Pull request (PR#6515)
  <https://github.com/micropython/micropython/pull/6515>`_


Load this module from the :doc:`esp<esp>` module. A simple example would be:

**Sender:** ::

    import network
    import espnow

    # A WLAN interface must be active to send()/recv()
    w0 = network.WLAN(network.STA_IF)  # Or network.AP_IF
    w0.active(True)
    w0.disconnect()   # For ESP8266

    e = espnow.ESPNow()
    e.active(True)
    peer = b'\xbb\xbb\xbb\xbb\xbb\xbb'   # MAC address of peer's wifi interface
    e.add_peer(peer)

    e.send("Starting...")       # Send to all peers
    for i in range(100):
        e.send(peer, str(i)*20, True)
        e.send(b'end')

**Receiver:** ::

    import network
    import espnow

    # A WLAN interface must be active to send()/recv()
    w0 = network.WLAN(network.STA_IF)
    w0.active(True)
    w0.disconnect()   # Because ESP8266 auto-connects to last Access Point

    e = espnow.ESPNow()
    e.active(True)
    peer = b'\xaa\xaa\xaa\xaa\xaa\xaa'   # MAC address of peer's wifi interface
    e.add_peer(peer)

    while True:
        host, msg = e.recv()
        if msg:             # msg == None if timeout in recv()
            print(host, msg)
            if msg == b'end':
                break

class ESPNow
------------

Constructor
-----------

.. class:: ESPNow()

    Returns the singleton ESPNow object. As this is a singleton, all calls to
    `espnow.ESPNow()` return a reference to the same object.

    .. note::
      Some methods are available only on the ESP32 due to code size
      restrictions on the ESP8266 and differences in the Espressif API.

Configuration
-------------

.. method:: ESPNow.active([flag])

    Initialise or de-initialise the ESPNow communication protocol depending on
    the value of the ``flag`` optional argument.

    .. data:: Arguments:

      - ``flag``: Any python value which can be converted to a boolean type.

        - ``True``: Prepare the software and hardware for use of the ESPNow
          communication protocol, including:

          - initialise the ESPNow data structures,
          - allocate the recv data buffer,
          - invoke esp_now_init() and
          - register the send and recv callbacks.

        - ``False``: De-initialise the Espressif ESPNow software stack
          (esp_now_deinit()), disable callbacks, deallocate the recv
          data buffer and deregister all peers.

    If ``flag`` is not provided, return the current status of the ESPNow
    interface.

    .. data:: Returns:

        ``True`` if interface is currently *active*, else ``False``.

.. method:: ESPNow.config(param=value, ...)
            ESPNow.config('param')   (ESP32 only)

    Set or get configuration values of the ESPNow interface. To set values, use
    the keyword syntax, and one or more parameters can be set at a time. To get
    a value the parameter name should be quoted as a string, and just one
    parameter is queried at a time.

    **Note:** *Getting* parameters is not supported on the ESP8266.

    .. data:: Options:

        ``rxbuf``: *(default=516)* Get/set the size in bytes of the internal
        buffer used to store incoming ESPNow packet data. The default size is
        selected to fit two max-sized ESPNow packets (250 bytes) with
        associated mac_address (6 bytes) and a message byte count (1 byte)
        plus buffer overhead. Increase this if you expect to receive a lot of
        large packets or expect bursty incoming traffic.

        **Note:** The recv buffer is allocated by `ESPNow.active()`. Changing
        this value will have no effect until the next call of
        `ESPNow.active(True)<ESPNow.active()>`.

        ``timeout``: *(default=300,000)* Default read timeout (in milliseconds).
        The timeout can also be provided as arg to `recvinto()`.

        ``rate``: Set the transmission speed for espnow packets. Must be set to
        a number from the allowed numeric values in `enum wifi_phy_rate_t
        <https://docs.espressif.com/projects/esp-idf/en/v4.4.1/esp32/
        api-reference/network/esp_wifi.html#_CPPv415wifi_phy_rate_t>`_.

    .. data:: Returns:

        ``None`` or the value of the parameter being queried.

    .. data:: Raises:

        - ``OSError(num, "ESP_ERR_ESPNOW_NOT_INIT")`` if not initialised.
        - ``ValueError()`` on invalid configuration options or values.

Sending and Receiving Data
--------------------------

A wifi interface (``network.STA_IF`` or ``network.AP_IF``) must be
`active()<network.WLAN.active>` before messages can be sent or received,
but it is not necessary to connect or configure the WLAN interface.
For example::

    import network

    w0 = network.WLAN(network.STA_IF)
    w0.active(True)
    w0.disconnect()    # For ESP8266

**Note:** The ESP8266 has a *feature* that causes it to automatically reconnect
to the last wifi Access Point when set `active(True)<network.WLAN.active>`
(even after reboot/reset). As noted below, this reduces the reliability of
receiving ESP-NOW messages. You can avoid this by calling
`disconnect()<network.WLAN.disconnect>` after
`active(True)<network.WLAN.active>`.

.. method:: ESPNow.send(mac, msg[, sync])
            ESPNow.send(msg)   (ESP32 only)

    Send the data contained in ``msg`` to the peer with given network ``mac``
    address. In the second form, ``mac=None`` and ``sync=True``. The peer must
    be registered with `ESPNow.add_peer()<ESPNow.add_peer()>` before the
    message can be sent.

    .. data:: Arguments:

      - ``mac``: byte string exactly 6 bytes long or ``None``. If ``mac`` is
        ``None`` (ESP32 only) the message will be sent to all registered peers,
        except any broadcast or multicast MAC addresses.

      - ``msg``: string or byte-string up to ``ESPNow.MAX_DATA_LEN`` (250)
        bytes long.

      - ``sync``:

        - ``True``: (default) send ``msg`` to the peer(s) and wait for a
          response (or not).

        - ``False`` send ``msg`` and return immediately. Responses from the
          peers will be discarded.

    .. data:: Returns:

      ``True`` if ``sync=False`` or if ``sync=True`` and *all* peers respond,
      else ``False``.

    .. data:: Raises:

      - ``OSError(num, "ESP_ERR_ESPNOW_NOT_INIT")`` if not initialised.
      - ``OSError(num, "ESP_ERR_ESPNOW_NOT_FOUND")`` if peer is not registered.
      - ``OSError(num, "ESP_ERR_ESPNOW_IF")`` the wifi interface is not
        `active()<network.WLAN.active>`.
      - ``OSError(num, "ESP_ERR_ESPNOW_NO_MEM")`` internal ESP-NOW buffers are
        full.
      - ``ValueError()`` on invalid values for the parameters.

    **Note**: A peer will respond with success if its wifi interface is
    `active()<network.WLAN.active>` and set to the same channel as the sender,
    regardless of whether it has initialised it's ESP-Now system or is
    actively listening for ESP-Now traffic (see the Espressif ESP-Now docs).

.. method:: ESPNow.recv([timeout])

    Wait for an incoming message and return the ``mac`` adress of the peer and
    the message. **Note**: It is **not** necessary to register a peer (using
    `add_peer()<ESPNow.add_peer()>`) to receive a message from that peer.

    .. data:: Arguments:

        ``timeout``: *(Optional)* If provided and not `None`, sets a timeout (in
        milliseconds) for the read. The default timeout (5 minutes) is set using
        `ESPNow.config()`. If ``timeout`` is less than zero, then wait forever.

    .. data:: Returns:

      - ``(None, None)`` if ``timeout`` before a message is received, or

      - ``[mac, msg]``: where:

        - ``mac`` is a bytestring containing the address of the device which
          sent the message, and
        - ``msg`` is a bytestring containing the message.

    .. data:: Raises:

      - ``OSError(num, "ESP_ERR_ESPNOW_NOT_INIT")`` if not initialised.
      - ``OSError(num, "ESP_ERR_ESPNOW_IF")`` the wifi interface is not
        `active()<network.WLAN.active>`.
      - ``ValueError()`` on invalid ``timeout`` values.

    `ESPNow.recv()` will allocate new storage for the returned list and the
    ``peer`` and ``msg`` bytestrings. This can lead to memory fragmentation if
    the data rate is high. See `ESPNow.irecv()` for a memory-friendly
    alternative.


.. method:: ESPNow.irecv([timeout])

    Works like `ESPNow.recv()` but will re-use internal bytearrays to store the
    return values: ``[mac, peer]``, so that no new memory is allocated on each
    call.

    .. data:: Arguments:

        ``timeout``: *(Optional)* Timeout in milliseconds (see `ESPNow.recv()`).

    .. data:: Returns:

      - As for `ESPNow.recv()`, except that ``msg`` is a bytearray, instead of
        a bytestring. On the ESP8266, ``mac`` will also be a bytearray.

    .. data:: Raises:

      - See `ESPNow.recv()`.

    **Note:** You may also read messages by iterating over the ESPNow object,
    which will use `irecv()` method for alloc-free reads, eg: ::

      import espnow
      e = espnow.ESPNow(); e.active(True)
      for mac, msg in e:
          print(mac, msg)
          if mac is None:   # mac, msg will equal (None, None) on timeout
              break

.. method:: ESPNow.recvinto(data[, timeout])

    Wait for an incoming message and return the length of the message in bytes.
    This is the low-level method used by both `recv()<ESPNow.recv()>` and
    `irecv()` to read messages.

    .. data:: Arguments:

        ``data``: A list of at least two elements, ``[peer, msg]``. ``msg`` must
        be a bytearray large enough to hold the message (250 bytes). On the
        ESP8266, ``peer`` should be a bytearray of 6 bytes. The MAC address of
        the sender and the message will be stored in these bytearrays (see Note
        on ESP32 below).

        ``timeout``: *(Optional)* Timeout in milliseconds (see `ESPNow.recv()`).

    .. data:: Returns:

      - Length of message in bytes or 0 if ``timeout`` is reached before a
        message is received.

    .. data:: Raises:

      - See `ESPNow.recv()`.

    **Note:** On the ESP32:

    - It is unnecessary to provide a bytearray in the first element of the
      ``data`` list because it will be replaced by a reference to a unique
      ``peer`` address in the **peer device table** (see `ESPNow.peers_table`).
    - If the list is at latest 4 elements long, the rssi and timestamp values
      will be saved as the 3rd and 4th elements.

.. method:: ESPNow.any()

    Check if data is available to be read with `ESPNow.recv()`.

    For more sophisticated querying of available characters use select.poll::

      import uselect as select
      import espnow

      e = espnow.ESPNow()
      poll = select.poll()
      poll.register(e, select.POLLIN)
      poll.poll(timeout)

    .. data:: Returns:

       ``True`` if data is available to be read, else ``False``.

.. method:: ESPNow.stats() (ESP32 only)

    .. data:: Returns:

      A 5-tuple containing the number of packets sent/received/lost:

      ``(tx_pkts, tx_responses, tx_failures, rx_packets, dropped_rx_packets)``

    Incoming packets are *dropped* when the recv buffers are full. To reduce
    packet loss, increase the ``rxbuf`` config parameters and ensure you are
    reading messages as quickly as possible.

    **Note**: Dropped packets will still be acknowledged to the sender as
    received.

Peer Management
---------------

The Espressif ESP-Now software requires that other devices (peers) must be
*registered* before we can `send()<ESPNow.send()>` them messages. It is
**not** necessary to *register* a peer to receive a message from that peer.

.. method:: ESPNow.set_pmk(pmk)

    Set the Primary Master Key (PMK) which is used to encrypt the Local Master
    Keys (LMK) for encrypting ESPNow data traffic. If this is not set, a
    default PMK is used by the underlying Espressif esp_now software stack.

    **Note:** messages will only be encrypted if ``lmk`` is also set in
    `ESPNow.add_peer()` (see `Security
    <https://docs.espressif.com/projects/esp-idf/en/latest/
    esp32/api-reference/network/esp_now.html#security>`_ in the Espressif API
    docs).

    .. data:: Arguments:

      ``pmk``: Must be a byte string, bytearray or string of length
      `espnow.KEY_LEN` (16 bytes).

    .. data:: Returns:

      ``None``

    .. data:: Raises:

      ``ValueError()`` on invalid ``pmk`` values.

.. method:: ESPNow.add_peer(mac, [lmk], [channel], [ifidx], [encrypt])
            ESPNow.add_peer(mac, param=value, ...)   (ESP32 only)

    Add/register the provided ``mac`` address as a peer. Additional parameters
    may also be specified as positional or keyword arguments:

    .. data:: Arguments:

        - ``mac``: The MAC address of the peer (as a 6-byte byte-string).

        - ``lmk``: The Local Master Key (LMK) key used to encrypt data
          transfers with this peer (unless the *encrypt* parameter is set to
          *False*). Must be:

          - a byte-string, bytearray ot string of length ``espnow.KEY_LEN`` (16
            bytes), or

          - any non ``True`` python value (default= ``b''``), signifying an
            *empty* key which will disable encryption.

        - ``channel``: The wifi channel (2.4GHz) to communicate with this peer.
          Must be an integer from 0 to 14. If channel is set to 0 the current
          channel of the wifi device will be used. (default=0)

        - ``ifidx``: *(ESP32 only)* Index of the wifi interface which will be
          used to send data to this peer. Must be an integer set to
          ``network.STA_IF`` (=0) or ``network.AP_IF`` (=1).
          (default=0/``network.STA_IF``). See `ESPNow and Wifi Operation`_
          below for more information.

        - ``encrypt``: *(ESP32 only)* If set to ``True`` data exchanged with
          this peer will be encrypted with the PMK and LMK. (default =
          ``False``)

        **ESP8266**: Keyword args may not be used on the ESP8266.

        **Note:** The maximum number of peers which may be registered is 20
        (`espnow.MAX_TOTAL_PEER_NUM`), with a maximum of 6
        (`espnow.MAX_ENCRYPT_PEER_NUM`) of those peers with encryption enabled
        (see `ESP_NOW_MAX_ENCRYPT_PEER_NUM <https://docs.espressif.com/
        projects/esp-idf/en/latest/esp32/api-reference/network/
        esp_now.html#c.ESP_NOW_MAX_ENCRYPT_PEER_NUM>`_ in the Espressif API
        docs).

    .. data:: Raises:

        - ``OSError(num, "ESP_ERR_ESPNOW_NOT_INIT")`` if not initialised.
        - ``OSError(num, "ESP_ERR_ESPNOW_EXIST")`` if ``mac`` is already
          registered.
        - ``OSError(num, "ESP_ERR_ESPNOW_FULL")`` if too many peers are
          already registered.
        - ``ValueError()`` on invalid keyword args or values.

.. method:: ESPNow.del_peer(mac)

    Deregister the peer associated with the provided ``mac`` address.

    .. data:: Returns:

        ``None``

    .. data:: Raises:

        - ``OSError(num, "ESP_ERR_ESPNOW_NOT_INIT")`` if not initialised.
        - ``OSError(num, "ESP_ERR_ESPNOW_NOT_FOUND")`` if ``mac`` is not
          registered.
        - ``ValueError()`` on invalid ``mac`` values.

.. method:: ESPNow.get_peer(mac) (ESP32 only)

    Return information on a registered peer.

    .. data:: Returns:

        ``(mac, lmk, channel, ifidx, encrypt)``: a tuple of the "peer
        info" associated with the ``mac`` address.

    .. data:: Raises:

        - ``OSError(num, "ESP_ERR_ESPNOW_NOT_INIT")`` if not initialised.
        - ``OSError(num, "ESP_ERR_ESPNOW_NOT_FOUND")`` if ``mac`` is not
          registered.
        - ``ValueError()`` on invalid ``mac`` values.

.. method:: ESPNow.peer_count() (ESP32 only)

    Return the number of registered peers:

    - ``(peer_num, encrypt_num)``: where

      - ``peer_num`` is the number of peers which are registered, and
      - ``encrypt_num`` is the number of encrypted peers.

.. method:: ESPNow.get_peers() (ESP32 only)

    Return the "peer info" parameters for all the registered peers (as a tuple
    of tuples).

.. method:: ESPNow.mod_peer(mac, lmk, [channel], [ifidx], [encrypt]) (ESP32 only)
            ESPNow.mod_peer(mac, 'param'=value, ...) (ESP32 only)

    Modify the parameters of the peer associated with the provided ``mac``
    address. Parameters may be provided as positional or keyword arguments
    (see `ESPNow.add_peer()`).

Callback Methods
----------------

.. method:: ESPNow.on_recv(recv_cb[, arg=None]) (ESP32 only)

  Set a callback function to be called *as soon as possible* after a message has
  been received from another ESPNow device. The function will be called with
  ``arg`` as an argument, eg: ::

          def recv_cb(e):
              print(e.irecv(0))
          e.on_recv(recv_cb, e)

.. method:: ESPNow.irq(irq_cb) (ESP32 only)

  Set a callback function to be called *as soon as possible* after a message has
  been received from another ESPNow device. The function will be called with
  `espnow.EVENT_RECV_MSG` as the first argument and a list of the peer and
  received message as the second argument, eg: ::

          def irq_cb(code, data):
              if code == espnow.EVENT_RECV_MSG:
                  peer, msg = data
                  print(peer, msg)
          e.irq(irq_cb)

  **Note:** `irq()<ESPNow.irq()>` and `on_recv()<ESPNow.on_recv()>` will each
  replace the current callback function, so only one of these methods will be
  active at any given time.

  The `on_recv()<ESPNow.on_recv()>` and `irq()<ESPNow.irq()>` callback methods
  are an alternative method for processing incoming espnow messages, especially
  if the data rate is moderate and the device is *not too busy* but there are
  some caveats:

  - The scheduler stack *can* easily overflow and callbacks will be missed if
    packets are arriving at a sufficient rate or if other micropython components
    (eg, bluetooth, machine.Pin.irq(), machine.timer, i2s, ...) are exercising
    the scheduler stack. This method may be less reliable for dealing with
    bursts of messages, or high throughput or on a device which is busy dealing
    with other hardware operations.

  - For more information on *scheduled* function callbacks see:
    `micropython.schedule()<micropython.schedule>`.

Constants
---------

.. data:: espnow.MAX_DATA_LEN(=250)
          espnow.KEY_LEN(=16)
          espnow.ETH_ALEN(=6)
          espnow.MAX_TOTAL_PEER_NUM(=20)
          espnow.MAX_ENCRYPT_PEER_NUM(=6)
          espnow.EVENT_RECV_MSG(=1)

Exceptions
----------

If the underlying Espressif ESPNow software stack returns an error code,
the micropython ESPNow module will raise an ``OSError(errnum, errstring)``
exception where ``errstring`` is set to the name of one of the error codes
identified in the
`Espressif ESP-Now docs
<https://docs.espressif.com/projects/esp-idf/en/latest/
api-reference/network/esp_now.html#api-reference>`_. For example::

    try:
        e.send(peer, 'Hello')
    except OSError as err:
        if len(err.args) < 2:
            raise err
        if err.args[1] == 'ESP_ERR_ESPNOW_NOT_INIT':
            e.active(True)
        elif err.args[1] == 'ESP_ERR_ESPNOW_NOT_FOUND':
            e.add_peer(peer)
        elif err.args[1] == 'ESP_ERR_ESPNOW_IF':
            network.WLAN(network.STA_IF).active(True)
        else:
            raise err

Wifi Signal Strength (RSSI) - (ESP32 only)
------------------------------------------

The ESPNow object maintains a **peer device table** which contains the signal
strength of the last received message for all known peers. The **peer device
table** can be accessed using `ESPNow.peers_table` and can be used to track
device proximity and identify *nearest neighbours* in a network of peer
devices. This feature is **not** available on ESP8266 devices.

.. data:: ESPNow.peers_table

    A reference to the **peer device table**: a dict of known peer devices
    and rssi values::

        {peer: [rssi, time_ms], ...}

    where:

    - ``peer`` is the peer MAC address (as `bytes`);
    - ``rssi`` is the wifi signal strength in dBm (-127 to 0) of the last
      message received from the peer; and
    - ``time_ms`` is the time the message was received (in milliseconds since
      system boot - wraps every 12 days).

    Example::

      >>> e.peers_table
      {b'\xaa\xaa\xaa\xaa\xaa\xaa': [-31, 18372],
       b'\xbb\xbb\xbb\xbb\xbb\xbb': [-43, 12541]}

    **Note**: the ``mac`` addresses returned by `recv()` are references to
    the ``peer`` key values in the **peer device table**.

    **Note**: RSSI and timestamp values in the device table are updated only
    when the message is read by the application.

Supporting asyncio
------------------

A supplementary module (`aioespnow`) is available to provide
:doc:`uasyncio<uasyncio>` support.

**Note:** Asyncio support is available on all ESP32 targets as well as those
ESP8266 boards which include the uasyncio module (ie. ESP8266 devices with at
least 2MB flash memory).

A small async server example::

    import network
    import aioespnow
    import uasyncio as asyncio

    # A WLAN interface must be active to send()/recv()
    network.WLAN(network.STA_IF).active(True)

    e = aioespnow.AIOESPNow()  # Returns AIOESPNow enhanced with async support
    e.active(True)
    peer = b'\xbb\xbb\xbb\xbb\xbb\xbb'
    e.add_peer(peer)

    # Send a periodic ping to a peer
    async def heartbeat(e, peer, period=30):
        while True:
            if not await e.asend(peer, b'ping'):
                print("Heartbeat: peer not responding:", peer)
            else:
                print("Heartbeat: ping", peer)
            await asyncio.sleep(period)

    # Echo any received messages back to the sender
    async def echo_server(e):
        async for mac, msg in e:
            print("Echo:", msg)
            try:
                await e.asend(mac, msg)
            except OSError as err:
                if len(err.args) > 1 and err.args[1] == 'ESP_ERR_ESPNOW_NOT_FOUND':
                    e.add_peer(mac)
                    await e.asend(mac, msg)

    async def main(e, peer, timeout, period):
        asyncio.create_task(heartbeat(e, peer, period))
        asyncio.create_task(echo_server(e))
        await asyncio.sleep(timeout)

    asyncio.run(main(e, peer, 120, 10))

.. module:: aioespnow
    :synopsis: ESP-NOW :doc:`uasyncio` support

.. class:: AIOESPNow()

    The `AIOESPNow` class inherits all the methods of `ESPNow<espnow.ESPNow>`
    and extends the interface with the following async methods.

.. method:: async AIOESPNow.arecv()

    Asyncio support for `ESPNow.recv()`. Note that this method does not take a
    timeout value as argument.

.. method:: async AIOESPNow.airecv()

    Asyncio support for `ESPNow.irecv()`. Note that this method does not take a
    timeout value as argument.

.. method:: async AIOESPNow.asend(mac, msg, sync=True)
            async AIOESPNow.asend(msg)

    Asyncio support for `ESPNow.send()`.

.. method:: __aiter__()/async __anext__()

    `AIOESPNow` also supports reading incoming messages by asynchronous
    iteration using ``async for``; eg::

      e = AIOESPNow()
      e.active(True)
      async def recv_till_halt(e):
          async for mac, msg in e:
              print(mac, msg)
              if msg == b'halt':
                break
      asyncio.run(recv_till_halt(e))

.. function:: ESPNow()

    Return an `AIOESPNow` object. This is a convenience function for adding
    async support to existing non-async code, eg: ::

      import network
      # import espnow
      import aioespnow as espnow

      e = espnow.ESPNow()  # Returns an AIOESPNow object
      e.active(True)
      ...

Broadcast and Multicast
-----------------------

All active ESP-Now clients will receive messages sent to their MAC address and
all devices (**except ESP8266 devices**) will also receive messages sent to the
``broadcast`` MAC address (``b'\xff\xff\xff\xff\xff\xff'``) or any multicast
MAC address.

All ESP-Now devices (including ESP8266 devices) can also send messages to the
``broadcast`` MAC address or any multicast MAC address.

To `send()<ESPNow.send()>` a broadcast message, the ``broadcast`` (or
multicast) MAC address must first be registered using
`add_peer()<ESPNow.add_peer()>`. `send()<ESPNow.send()>` will always return
``True`` for broadcasts, regardless of whether any devices receive the
message. It is not permitted to encrypt messages sent to the ``broadcast``
address or any multicast address.

**Note**: `ESPNow.send(None, msg)<ESPNow.send()>` will send to all registered
peers *except* the broadcast address. To send a broadcast or multicast
message, you must specify the ``broadcast`` (or multicast) MAC address as the
peer. For example::

    bcast = b'\xff' * 6
    e.add_peer(bcast)
    e.send(bcast, "Hello World!")

ESPNow and Wifi Operation
-------------------------

ESPNow messages may be sent and received on any `active()<network.WLAN.active>`
`WLAN<network.WLAN()>` interface (``network.STA_IF`` or ``network.AP_IF``), even
if that interface is also connected to a wifi network or configured as an access
point. When an ESP32 or ESP8266 device connects to a Wifi Access Point (see
`ESP32 Quickref <../esp32/quickref.html#networking>`__) the following things
happen which affect ESPNow communications:

1. Power saving mode (``ps_mode=WIFI_PS_MIN_MODEM``) is automatically activated;
   and
2. The radio on the esp device changes wifi ``channel`` to match the channel
   used by the Access Point.

**Power Saving Mode:** The power saving mode causes the device to turn off the
radio periodically (search the internet for "DTIM Interval" for further
details), making it unreliable in receiving ESPNow messages. There are several
options to improve reliability of receiving ESPNow packets when also connected
to a wifi network:

1. Disable the power-saving mode on the STA_IF interface:

   - Use ``w0.config(ps_mode=WIFI_PS_NONE)``
   - This requires the ESPNow patches on ESP32 (not supported in micropython
     as of v1.19).

2. Turning on the AP_IF interface will also disable the power saving mode. So
   long as the AP_IF interface is also active, receiving messages through the
   STA_IF interface will work reliably. However, the device will then be
   advertising an active wifi access point.

   - Since the AP_IF interface is active you **may** also choose to send and
     receive your messages via the AP_IF interface, but this is not necessary.

3. Configure ESPNow clients to retry sending messages.

**Managing wifi channels:** Any other espnow devices wishing to communicate with
a device which is also connected to a Wifi Access Point MUST use the same
channel. A common scenario is where one espnow device is connected to a wifi
router and acts as a proxy for messages from a group of sensors connected via
espnow:

**Proxy:** ::

  import time, network, espnow

  e = espnow.ESPNow(); e.active(True)
  w0 = network.WLAN(network.STA_IF); w0.active(True)
  w0.connect('myssid', 'myppassword')
  while not w0.isconnected():            # Wait until connected...
      time.sleep(0.1)
  w0.config(ps_mode=network.WIFI_PS_NONE)  # ..then disable power saving
  # network.WLAN(network.AP_IF).active(True)  # Alternative to above

  # Print the wifi channel used AFTER connecting to access point
  print("Proxy running on channel:", w0.config("channel"))

  for peer, msg in e:
      # Receive espnow messages and forward them to MQTT broker over wifi

**Sensor:** ::

  import network, espnow

  peer = b'0\xaa\xaa\xaa\xaa\xaa'  # MAC address of proxy
  e = espnow.ESPNow(); e.active(True);
  w0 = network.WLAN(network.STA_IF); w0.active(True); w0.disconnect()
  w0.config(channel=6)    # Change to the channel used by the proxy above.
  e.add_peer(peer)
  while True:
      msg = read_sensor()
      e.send(peer, msg)

Other issues to take care with when using ESPNow with wifi are:

- If the AP_IF interface is active while the STA_IF is also connected to a Wifi
  Access Point, the AP_IF will always operate on the same channel as the STA_IF;
  regardless of the channel you set for the AP_IF (see `Attention Note 3
  <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html#_CPPv419esp_wifi_set_config16wifi_interface_tP13wifi_config_t>`_
  ). After all, there is really only one wifi radio on the device, which is
  shared by the STA_IF and AP_IF virtual devices.

- If the esp device is connected to a Wifi Access Point that goes down, the
  device will start scanning channels in an attempt to reconnect to the Access
  Point. This means espnow messages will be lost while scanning for the AP. This
  can be disabled by ``w0.config(reconnects=0)``, which will also disable the
  automatic reconnection after losing connection.

- Some versions of the ESP IDF only permit sending ESPNow packets from the
  STA_IF interface to peers which have been registered on the same wifi
  channel as the STA_IF::

    ESPNOW: Peer channel is not equal to the home channel, send fail!

- Some versions of the ESP IDF don't permit setting the channel of the STA_IF
  at all, other than by connecting to an Access Point (This seems to be fixed
  in IDF 4+). Micropython versions without the ESPNow patches also provide no
  support for setting the channel of the STA_IF.

ESPNow and Sleep Modes
----------------------

The `machine.lightsleep([time_ms])<machine.lightsleep>` and
`machine.deepsleep([time_ms])<machine.deepsleep>` functions can be used to put
the ESP32 and periperals (including the WiFi and Bluetooth radios) to sleep.
This is useful in many applications to conserve battery power. However,
applications must disable the WLAN peripheral (using
`active(False)<network.WLAN.active>`) before entering light or deep sleep (see
`Sleep Modes <https://docs.espressif.com/
projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html>`_).
Otherwise the WiFi radio may not be initialised properly after wake from
sleep. If the ``STA_IF`` and ``AP_IF`` interfaces have both been set
`active(True)<network.WLAN.active()>` then both interfaces should be set
`active(False)<network.WLAN.active()>` before entering any sleep mode.

**Example:** deep sleep::

  import network
  import machine
  import espnow

  peer = b'0\xaa\xaa\xaa\xaa\xaa'        # MAC address of peer
  e = espnow.ESPNow()
  e.active(True)

  w0 = network.WLAN(network.STA_IF)
  w0.active(True)
  e.add_peer(peer)                       # Register peer on STA_IF

  print('Sending ping...')
  if not e.send(peer, b'ping'):
    print('Ping failed!')

  e.active(False)
  w0.active(False)                       # Disable the wifi before sleep

  print('Going to sleep...')
  machine.deepsleep(10000)               # Sleep for 10 seconds then reboot

**Example:** light sleep::

  import network
  import machine
  import espnow

  peer = b'0\xaa\xaa\xaa\xaa\xaa'        # MAC address of peer
  e = espnow.ESPNow()
  e.active(True)

  w0 = network.WLAN(network.STA_IF)
  w0.active(True)                        # Set channel will fail unless Active
  w0.config(channel=6)
  e.add_peer(peer)                       # Register peer on STA_IF

  while True:
    print('Sending ping...')
    if not e.send(peer, b'ping'):
      print('Ping failed!')

    w0.active(False)                     # Disable the wifi before sleep

    print('Going to sleep...')
    machine.lightsleep(10000)            # Sleep for 10 seconds

    w0.active(True)
    w0.config(channel=6)                 # Wifi loses config after lightsleep()

