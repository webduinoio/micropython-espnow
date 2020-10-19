/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017-2020 Nick Moore
 * Copyright (c) 2018 shawwwn <shawwwn1@gmail.com>
 * Copyright (c) 2020-2021 Glenn Moloney @glenn20
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "py/objstr.h"
#include "py/objarray.h"
#include "py/stream.h"

#include "mpconfigport.h"
#include "mphalport.h"
#include "modnetwork.h"
#include "ring_buffer.h"
#include "esp_espnow.h"

static const uint8_t ESPNOW_MAGIC = 0x99;

// ESPNow packet format for the receive buffer.
typedef struct {
    uint8_t magic;              // = ESPNOW_MAGIC
    uint8_t msg_len;            // Length of the message
    uint8_t peer[6];            // Peer address
    uint8_t msg[0];             // Message is up to 250 bytes
} __attribute__((packed)) espnow_pkt_t;

// Use this for peeking at the header of the next packet in the buffer.
typedef struct {
    uint8_t magic;              // = ESPNOW_MAGIC
    uint8_t msg_len;            // Length of the message
} __attribute__((packed)) espnow_hdr_t;

// The maximum length of an espnow packet (bytes)
static const size_t MAX_PACKET_LEN = (
    (sizeof(espnow_pkt_t) + ESP_NOW_MAX_DATA_LEN));

// Enough for 2 full-size packets: 2 * (6 + 2 + 250) = 516 bytes
// Will allocate an additional 7 bytes for buffer overhead
static const size_t DEFAULT_RECV_BUFFER_SIZE = (2 * MAX_PACKET_LEN);

// Default timeout (millisec) to wait for incoming ESPNow messages (5 minutes).
static const size_t DEFAULT_RECV_TIMEOUT_MS = (5 * 60 * 1000);

// Time to wait (millisec) for responses from sent packets: (2 seconds).
static const size_t DEFAULT_SEND_TIMEOUT_MS = (2 * 1000);

// Number of milliseconds to wait (mp_hal_wait_ms()) in each loop
// while waiting for send or receive packet.
// Needs to be >15ms to permit yield to other tasks.
static const size_t BUSY_WAIT_MS = 25;

// The data structure for the espnow_singleton.
typedef struct _esp_espnow_obj_t {
    mp_obj_base_t base;
    buffer_t recv_buffer;           // A buffer for received packets
    espnow_pkt_t *irecv_packet;     // Storage for packet return from irecv()
    mp_obj_tuple_t *irecv_tuple;    // Preallocated tuple for irecv()
    mp_obj_tuple_t *none_tuple;     // Preallocated tuple for irecv()
    size_t recv_buffer_size;        // The size of the recv_buffer
    size_t recv_timeout_ms;         // Timeout for recv()/irecv()/poll()/ipoll()
    volatile size_t rx_packets;     // # of received packets
    size_t dropped_rx_pkts;         // # of dropped packets (buffer full)
    size_t tx_packets;              // # of sent packets
    volatile size_t tx_responses;   // # of sent packet responses received
    volatile size_t tx_failures;    // # of sent packet responses failed
    size_t peer_count;              // Cache the # of peers for send(sync=True)
    mp_obj_t recv_cb;               // Callback when a packet is received
} esp_espnow_obj_t;

// Defined below.
const mp_obj_type_t esp_espnow_type;

// ### Initialisation and Config functions
//

#define INITIALISED         (1)

// Return a pointer to the ESPNow module singleton
// If state == INITIALISED check the device has been initialised.
// Raises OSError if not initialised and state == INITIALISED.
static esp_espnow_obj_t *get_singleton(int state) {
    esp_espnow_obj_t *self = MP_STATE_PORT(espnow_singleton);
    // assert(self);
    if (state == INITIALISED && self->recv_buffer == NULL) {
        // Throw an espnow not initialised error
        check_esp_err(ESP_ERR_ESPNOW_NOT_INIT);
    }
    return self;
}

// Allocate and initialise the ESPNow module as a singleton.
// Returns the initialised espnow_singleton.
STATIC mp_obj_t espnow_make_new(const mp_obj_type_t *type, size_t n_args,
    size_t n_kw, const mp_obj_t *all_args) {

    // The espnow_singleton must be defined in MICROPY_PORT_ROOT_POINTERS
    // (see mpconfigport.h) to prevent memory allocated here from being
    // garbage collected.
    // NOTE: on soft reset the espnow_singleton MUST be set to NULL and the
    // ESP-NOW functions de-initialised (see main.c).
    esp_espnow_obj_t *self = MP_STATE_PORT(espnow_singleton);
    if (self != NULL) {
        return self;
    }
    self = m_malloc0(sizeof(esp_espnow_obj_t));
    self->base.type = &esp_espnow_type;
    self->recv_buffer_size = DEFAULT_RECV_BUFFER_SIZE;
    self->recv_timeout_ms = DEFAULT_RECV_TIMEOUT_MS;
    self->recv_buffer = NULL;
    self->irecv_tuple = NULL;
    self->none_tuple = NULL;

    // Allocate and initialise the "callee-owned" tuple for irecv().
    espnow_pkt_t *pkt = (espnow_pkt_t *)m_malloc0(MAX_PACKET_LEN);
    pkt->magic = ESPNOW_MAGIC;
    pkt->msg_len = 0;
    // Build a tuple of bytearrays. The first bytearray points to
    // peer mac address and the second to message in the packet buffer.
    self->irecv_packet = pkt;
    self->irecv_tuple = mp_obj_new_tuple(2,
        (mp_obj_t[]) {
        mp_obj_new_bytearray_by_ref(ESP_NOW_ETH_ALEN, pkt->peer),
        mp_obj_new_bytearray_by_ref(ESP_NOW_MAX_DATA_LEN, pkt->msg),
    });
    self->none_tuple = mp_obj_new_tuple(
        2, (mp_obj_t[]) {mp_const_none, mp_const_none});
    self->recv_cb = mp_const_none;

    // Set the global singleton pointer for the espnow protocol.
    MP_STATE_PORT(espnow_singleton) = self;

    return self;
}

// Forward declare the send and recv ESPNow callbacks
STATIC void send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);

STATIC void recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len);

// ESPNow.init(): Initialise the data buffers and ESP-NOW functions.
// Initialise the Espressif ESPNOW software stack, register callbacks and
// allocate the recv data buffers.
// Returns None.
STATIC mp_obj_t espnow_init(mp_obj_t _) {
    esp_espnow_obj_t *self = get_singleton(0);
    if (self->recv_buffer == NULL) {    // Already initialised
        self->recv_buffer = buffer_init(self->recv_buffer_size);
        self->recv_buffer_size = buffer_size(self->recv_buffer);

        esp_initialise_wifi();  // Call the wifi init code in network_wifi.c
        check_esp_err(esp_now_init());
        check_esp_err(esp_now_register_recv_cb(recv_cb));
        check_esp_err(esp_now_register_send_cb(send_cb));
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(espnow_init_obj, espnow_init);

// ESPNow.deinit(): De-initialise the ESPNOW software stack, disable callbacks
// and deallocate the recv data buffers.
// Note: this function is called from main.c:mp_task() to cleanup before soft
// reset, so cannot be declared STATIC and must guard against self == NULL;.
mp_obj_t espnow_deinit(mp_obj_t _) {
    esp_espnow_obj_t *self = get_singleton(0);
    if (self != NULL && self->recv_buffer != NULL) {
        check_esp_err(esp_now_unregister_recv_cb());
        check_esp_err(esp_now_unregister_send_cb());
        check_esp_err(esp_now_deinit());
        buffer_release(self->recv_buffer);
        self->recv_buffer = NULL;
        self->peer_count = 0; // esp_now_deinit() removes all peers.
        self->tx_packets = self->tx_responses;
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(espnow_deinit_obj, espnow_deinit);

// ESPNow.config(['param'|param=value, ..])
// Get or set configuration values. Supported config params:
//    rxbuf: size of internal buffer for rx packets (default=514 bytes)
//    timeout: Default read timeout (default=300,000 milliseconds)
//    on_recv: Set callback function to be invoked when a message is received.
STATIC mp_obj_t espnow_config(
    size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    esp_espnow_obj_t *self = get_singleton(0);
    enum { ARG_get, ARG_rxbuf, ARG_timeout, ARG_on_recv };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_get, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_rxbuf, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_on_recv, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_rxbuf].u_int >= 0) {
        self->recv_buffer_size = args[ARG_rxbuf].u_int;
    }
    if (args[ARG_timeout].u_int >= 0) {
        self->recv_timeout_ms = args[ARG_timeout].u_int;
    }
    if (args[ARG_on_recv].u_obj != MP_OBJ_NULL) {
        self->recv_cb = args[ARG_on_recv].u_obj;
    }
    if (args[ARG_get].u_obj == MP_OBJ_NULL) {
        return mp_const_none;
    }
#define QS(x) (uintptr_t)MP_OBJ_NEW_QSTR(x)
    // Return the value of the requested parameter
    uintptr_t name = (uintptr_t)args[ARG_get].u_obj;
    if (name == QS(MP_QSTR_rxbuf)) {
        return mp_obj_new_int(
            (self->recv_buffer
                ? buffer_size(self->recv_buffer)
                : self->recv_buffer_size));
    } else if (name == QS(MP_QSTR_timeout)) {
        return mp_obj_new_int(self->recv_timeout_ms);
    } else if (name == QS(MP_QSTR_on_recv)) {
        return self->recv_cb;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("unknown config param"));
    }
#undef QS

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(espnow_config_obj, 1, espnow_config);

// ESPnow.stats(): Provide some useful stats.
// Returns a tuple of:
//   (tx_pkts, tx_responses, tx_failures, rx_pkts, dropped_rx_pkts)
STATIC mp_obj_t espnow_stats(mp_obj_t _) {
    const esp_espnow_obj_t *self = get_singleton(0);
    mp_obj_t items[] = {
        mp_obj_new_int(self->tx_packets),
        mp_obj_new_int(self->tx_responses),
        mp_obj_new_int(self->tx_failures),
        mp_obj_new_int(self->rx_packets),
        mp_obj_new_int(self->dropped_rx_pkts),
    };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(items), items);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(espnow_stats_obj, espnow_stats);

STATIC mp_obj_t espnow_version(mp_obj_t _) {
    uint32_t version;
    check_esp_err(esp_now_get_version(&version));
    return mp_obj_new_int(version);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(espnow_version_obj, espnow_version);

// ### The ESP_Now send and recv callback routines
//

// Callback triggered when a sent packet is acknowledged by the peer (or not).
// Just count the number of responses and number of failures.
// These are used in the send()/write() logic.
STATIC void send_cb(
    const uint8_t *mac_addr, esp_now_send_status_t status) {

    esp_espnow_obj_t *self = get_singleton(0);
    self->tx_responses++;
    if (status != ESP_NOW_SEND_SUCCESS) {
        self->tx_failures++;
    }
}

// Callback triggered when an ESP-Now packet is received.
// Write the peer MAC address and the message into the recv_buffer as an
// ESPNow packet.
// If the buffer is full, drop the message and increment the dropped count.
// Schedules the user callback if one has been registered (ESPNow.config()).
STATIC void recv_cb(
    const uint8_t *mac_addr, const uint8_t *msg, int msg_len) {

    esp_espnow_obj_t *self = get_singleton(0);
    if (sizeof(espnow_pkt_t) + msg_len >= buffer_free(self->recv_buffer)) {
        self->dropped_rx_pkts++;
        return;
    }
    buffer_t buf = self->recv_buffer;
    const espnow_hdr_t header = {
        .magic = ESPNOW_MAGIC,
        .msg_len = msg_len
    };
    buffer_put(buf, &header, sizeof(header));
    buffer_put(buf, mac_addr, ESP_NOW_ETH_ALEN);
    buffer_put(buf, msg, msg_len);
    self->rx_packets++;
    if (self->recv_cb != mp_const_none) {
        mp_sched_schedule(self->recv_cb, self);
    }
}

// ### Handling espnow packets in the recv buffer
//

// Check the packet header provided and return the packet length.
// Raises ValueError if the header is bad or the packet is larger than max_size.
// Bypass the size check if max_size == 0.
// Returns the packet length in bytes (including header).
static int _check_packet_length(espnow_hdr_t *header, size_t max_size) {
    if (header->magic != ESPNOW_MAGIC ||
        header->msg_len > ESP_NOW_MAX_DATA_LEN) {
        mp_raise_ValueError(MP_ERROR_TEXT("ESP-Now: Bad packet"));
    }
    int pkt_len = header->msg_len + sizeof(espnow_pkt_t);
    if (max_size > 0 && max_size < pkt_len) {
        mp_raise_ValueError(MP_ERROR_TEXT("ESP-Now: Buffer too small for packet"));
    }
    return pkt_len;
}

// Copy the next packet from the recv buffer to buf_out.
// Raises ValueError if the header is bad or the packet is larger than max_size.
// Returns the length of the packet or 0 if there is no packet available.
static int _get_packet(
    buffer_t buffer, void *buf_out, size_t max_size, int timeout_ms) {

    espnow_pkt_t *pkt = buf_out;
    if (!buffer_recv(buffer, pkt, sizeof(*pkt), timeout_ms)) {
        return 0;
    }
    int pkt_len = _check_packet_length((espnow_hdr_t *)pkt, 0);
    if (!buffer_get(buffer, pkt->msg, pkt->msg_len)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Buffer error"));
    }
    return pkt_len;
}

// ### Send and Receive ESP_Now data
//

// ESPNow.irecv([timeout]):
// Like ESPNow.recv() but returns a "callee-owned" tuple of byte strings.
// This provides an allocation-free way to read successive messages.
// Beware: the tuple and bytestring storage is re-used between all calls
// to irecv().
// Takes an optional "timeout" argument in milliseconds.
// Default timeout is set with ESPNow.config(timeout=milliseconds).
// Returns (None, None) on timeout.
STATIC mp_obj_t espnow_irecv(size_t n_args, const mp_obj_t *args) {
    esp_espnow_obj_t *self = get_singleton(INITIALISED);

    size_t timeout_ms = (
        (n_args > 1) ? mp_obj_get_int(args[1]) : self->recv_timeout_ms);

    // Read the packet header from the incoming buffer
    mp_obj_array_t *msg = self->irecv_tuple->items[1];
    espnow_pkt_t *pkt = self->irecv_packet;
    if (_get_packet(self->recv_buffer, pkt, sizeof(*pkt), timeout_ms) == 0) {
        msg->len = 0;               // Set callee-owned msg bytearray to empty.
        return self->none_tuple;    // Return tuple(None, None)
    }
    msg->len = pkt->msg_len;
    return MP_OBJ_FROM_PTR(self->irecv_tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(espnow_irecv_obj, 1, 2, espnow_irecv);

// ESPNow.recv([timeout]):
// Returns a tuple of byte strings: (peer_addr, message) where peer_addr is
// the MAC address of the sending peer.
// Takes an optional "timeout" argument in milliseconds.
// Default timeout is set with ESPNow.config(timeout=milliseconds).
// Return (None, None) on timeout.
STATIC mp_obj_t espnow_recv(size_t n_args, const mp_obj_t *args) {
    esp_espnow_obj_t *self = get_singleton(INITIALISED);
    size_t timeout_ms = (
        (n_args > 1) ? mp_obj_get_int(args[1]) : self->recv_timeout_ms);

    // Read the packet header from the incoming buffer
    espnow_hdr_t header;
    if (!buffer_recv(self->recv_buffer, &header, sizeof(header), timeout_ms)) {
        return self->none_tuple;       // Buffer is empty
    }
    int msg_len = _check_packet_length(&header, 0) - sizeof(espnow_pkt_t);

    // Allocate vstrs as new storage buffers for the mac address and message.
    // The storage will be handed over to mp_obj_new_str_from_vstr() below.
    vstr_t peer_addr, message;
    vstr_init_len(&peer_addr, ESP_NOW_ETH_ALEN);
    vstr_init_len(&message, msg_len);

    // Now read the peer_address and message into the byte strings.
    if (!buffer_get(self->recv_buffer, peer_addr.buf, ESP_NOW_ETH_ALEN) ||
        !buffer_get(self->recv_buffer, message.buf, msg_len)) {
        vstr_clear(&peer_addr);
        vstr_clear(&message);
        mp_raise_ValueError(MP_ERROR_TEXT("Buffer error"));
    }

    // Create and return a tuple of byte strings: (mac_addr, message)
    mp_obj_t items[] = {
        mp_obj_new_str_from_vstr(&mp_type_bytes, &peer_addr),
        mp_obj_new_str_from_vstr(&mp_type_bytes, &message),
    };
    return mp_obj_new_tuple(2, items);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(espnow_recv_obj, 1, 2, espnow_recv);

// Test if data is available to read from the buffers
STATIC mp_obj_t espnow_poll(const mp_obj_t _) {
    esp_espnow_obj_t *self = get_singleton(INITIALISED);

    return buffer_empty(self->recv_buffer) ? mp_const_false : mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(espnow_poll_obj, espnow_poll);

// Used by _do_espnow_send() for sends() with sync==True.
// Wait till all pending sent packet responses have been received.
// ie. self->tx_responses == self->tx_packets.
// Return the number of responses where status != ESP_NOW_SEND_SUCCESS.
static void _wait_for_pending_responses(esp_espnow_obj_t *self) {
    int64_t start = mp_hal_ticks_ms();
    // Note: the send timeout is just a fallback - in normal operation
    // we should never reach that timeout.
    while (self->tx_responses < self->tx_packets &&
           (mp_hal_ticks_ms() - start) <= DEFAULT_SEND_TIMEOUT_MS) {
        // Won't yield unless delay > portTICK_PERIOD_MS (10ms)
        mp_hal_delay_ms(BUSY_WAIT_MS);
    }
    if (self->tx_responses != self->tx_packets) {
        mp_raise_ValueError(MP_ERROR_TEXT("Send timeout on synch."));
    }
}

// Send an ESPNow message to the peer_addr and optionally wait for the
// send response.
// Returns the number of "Not received" responses (which may be more than
// one if the send is to all peers).
static int _do_espnow_send(
    esp_espnow_obj_t *self, const uint8_t *peer_addr,
    const uint8_t *message, size_t length, bool sync) {

    if (sync) {
        // If the last call was sync==False there may be outstanding responses
        // still to be received (possible many if we just had a burst of
        // unsync send()s). We need to wait for all pending responses if this
        // call has sync=True.
        // Flush out any pending responses.
        _wait_for_pending_responses(self);
    }
    int saved_failures = self->tx_failures;
    // Send the packet - try, try again if internal esp-now buffers are full.
    esp_err_t err;
    int64_t start = mp_hal_ticks_ms();
    while ((ESP_ERR_ESPNOW_NO_MEM ==
            (err = esp_now_send(peer_addr, message, length))) &&
           (mp_hal_ticks_ms() - start) <= DEFAULT_SEND_TIMEOUT_MS) {
        // Won't yield unless delay > portTICK_PERIOD_MS (10ms)
        mp_hal_delay_ms(BUSY_WAIT_MS);
    }
    check_esp_err(err);           // Will raise OSError if e != ESP_OK
    // Increment the sent packet count. If peer_addr==NULL msg will be
    // sent to all peers EXCEPT any broadcast or multicast addresses.
    self->tx_packets += ((peer_addr == NULL) ? self->peer_count : 1);
    if (sync) {
        // Wait for and tally all the expected responses from peers
        _wait_for_pending_responses(self);
    }
    // Return number of non-responsive peers.
    return self->tx_failures - saved_failures;
}

// Return C pointer to byte memory string/bytes/bytearray in obj.
// Raise ValueError if the length does not match expected len.
static const uint8_t *_get_bytes_len(mp_obj_t obj, size_t len) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(obj, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len != len) {
        mp_raise_ValueError(MP_ERROR_TEXT("wrong length"));
    }
    return (const uint8_t *)bufinfo.buf;
}

// Return C pointer to the MAC address.
// Raise ValueError if mac_addr is wrong type or is not 6 bytes long.
static const uint8_t *_get_peer(mp_obj_t mac_addr) {
    return mp_obj_is_true(mac_addr)
        ? _get_bytes_len(mac_addr, ESP_NOW_ETH_ALEN) : NULL;
}

// ESPNow.send(peer_addr, message, [sync (=true)])
// ESPNow.send(message)
// Send a message to the peer's mac address. Optionally wait for a response.
// If peer_addr == None, send to all registered peers.
// If sync == True, wait for response after sending.
// Returns:
//   True  if sync==False and message sent successfully.
//   True  if sync==True and message is received successfully by all recipients
//   False if sync==True and message is not received by at least one recipient
// Raises: EAGAIN if the internal espnow buffers are full.
STATIC mp_obj_t espnow_send(size_t n_args, const mp_obj_t *args) {
    esp_espnow_obj_t *self = get_singleton(INITIALISED);
    // Check the various combinations of input arguments
    mp_obj_t peer = (n_args > 2) ? args[1] : mp_const_none;
    mp_obj_t msg = (n_args > 2) ? args[2] : (n_args == 2) ? args[1] : MP_OBJ_NULL;
    mp_obj_t sync = (n_args > 3) ? args[3] : mp_const_true;

    // Get a pointer to the data buffer of the message
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(msg, &bufinfo, MP_BUFFER_READ);

    int failed_responses = _do_espnow_send(
        self, _get_peer(peer), bufinfo.buf, bufinfo.len, mp_obj_is_true(sync));
    return (failed_responses == 0) ? mp_const_true : mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(espnow_send_obj, 2, 4, espnow_send);

// ### Peer Management Functions
//

// Set the ESP-NOW Primary Master Key (pmk) (for encrypted communications).
// Raise OSError if ESP-NOW functions are not initialised.
// Raise ValueError if key is not a bytes-like object exactly 16 bytes long.
STATIC mp_obj_t espnow_set_pmk(mp_obj_t _, mp_obj_t key) {
    check_esp_err(esp_now_set_pmk(_get_bytes_len(key, ESP_NOW_KEY_LEN)));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(espnow_set_pmk_obj, espnow_set_pmk);

// Common code for add_peer() and mod_peer() to process the args and kw_args:
// Raise ValueError if the LMK is not a bytes-like object of exactly 16 bytes.
// Raise TypeError if invalid keyword args or too many positional args.
// Return true if all args parsed correctly.
STATIC bool _update_peer_info(
    esp_now_peer_info_t *peer, size_t n_args,
    const mp_obj_t *pos_args, mp_map_t *kw_args) {

    enum { ARG_lmk, ARG_channel, ARG_ifidx, ARG_encrypt };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_lmk, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_channel, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_ifidx, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_encrypt, MP_ARG_BOOL, {.u_bool = MP_OBJ_NULL} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    if (args[ARG_lmk].u_obj != MP_OBJ_NULL) {
        mp_obj_t obj = args[ARG_lmk].u_obj;
        peer->encrypt = mp_obj_is_true(obj);
        if (peer->encrypt) {
            // Key can be <= 16 bytes - padded with '\0'.
            memcpy(peer->lmk,
                _get_bytes_len(obj, ESP_NOW_KEY_LEN),
                ESP_NOW_KEY_LEN);
        }
    }
    if (args[ARG_channel].u_int != -1) {
        peer->channel = args[ARG_channel].u_int;
    }
    if (args[ARG_ifidx].u_int != -1) {
        peer->ifidx = args[ARG_ifidx].u_int;
    }
    if (args[ARG_encrypt].u_obj != MP_OBJ_NULL) {
        peer->encrypt = args[ARG_encrypt].u_bool;
    }
    return true;
}

// Update the cached peer count in self->peer_count;
// The peer_count is used for the send()/write() logic and is updated
// from add_peer(), mod_peer() and del_peer().
STATIC void _update_peer_count() {
    esp_espnow_obj_t *self = get_singleton(INITIALISED);
    esp_now_peer_num_t peer_num = {0};
    check_esp_err(esp_now_get_peer_num(&peer_num));
    self->peer_count = peer_num.total_num;

    // Check if the the broadcast MAC address is registered
    uint8_t broadcast[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    esp_now_peer_info_t peer = {0};
    if (esp_now_get_peer(broadcast, &peer) == ESP_OK) {
        // Don't count the broadcast address
        self->peer_count--;
    }
}

// ESPNow.add_peer(peer_mac, [lmk, [channel, [ifidx, [encrypt]]]]) or
// ESPNow.add_peer(peer_mac, [lmk=b'0123456789abcdef'|b''|None|False],
//          [channel=1..11|0], [ifidx=0|1], [encrypt=True|False])
// Positional args set to None will be left at defaults.
// Raise OSError if ESPNow.init() has not been called.
// Raise ValueError if mac or LMK are not bytes-like objects or wrong length.
// Raise TypeError if invalid keyword args or too many positional args.
// Return None.
STATIC mp_obj_t espnow_add_peer(
    size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, _get_peer(args[1]), ESP_NOW_ETH_ALEN);
    _update_peer_info(&peer, n_args - 2, args + 2, kw_args);

    check_esp_err(esp_now_add_peer(&peer));
    _update_peer_count();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(espnow_add_peer_obj, 2, espnow_add_peer);

// ESPNow.mod_peer(peer_mac, [lmk, [channel, [ifidx, [encrypt]]]]) or
// ESPNow.mod_peer(peer_mac, [lmk=b'0123456789abcdef'|b''|None|False],
//          [channel=1..11|0], [ifidx=0|1], [encrypt=True|False])
// Positional args set to None will be left at current values.
// Raise OSError if ESPNow.init() has not been called.
// Raise ValueError if mac or LMK are not bytes-like objects or wrong length.
// Raise TypeError if invalid keyword args or too many positional args.
// Return None.
STATIC mp_obj_t espnow_mod_peer(
    size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, _get_peer(args[1]), ESP_NOW_ETH_ALEN);
    check_esp_err(esp_now_get_peer(peer.peer_addr, &peer));

    _update_peer_info(&peer, n_args - 2, args + 2, kw_args);

    check_esp_err(esp_now_mod_peer(&peer));
    _update_peer_count();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(espnow_mod_peer_obj, 2, espnow_mod_peer);

// ESPNow.get_peer(peer_mac): Get the peer info for peer_mac as a tuple.
// Raise OSError if ESPNow.init() has not been called.
// Raise ValueError if mac or LMK are not bytes-like objects or wrong length.
// Return a tuple of (peer_addr, lmk, channel, ifidx, encrypt).
STATIC mp_obj_t espnow_get_peer(mp_obj_t _, mp_obj_t arg1) {
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, _get_peer(arg1), ESP_NOW_ETH_ALEN);

    check_esp_err(esp_now_get_peer(peer.peer_addr, &peer));

    // Return a tuple of (peer_addr, lmk, channel, ifidx, encrypt)
    mp_obj_t items[] = {
        mp_obj_new_bytes(peer.peer_addr, sizeof(peer.peer_addr)),
        mp_obj_new_bytes(peer.lmk, sizeof(peer.lmk)),
        mp_obj_new_int(peer.channel),
        mp_obj_new_int(peer.ifidx),
        (peer.encrypt) ? mp_const_true : mp_const_false,
    };

    return mp_obj_new_tuple(5, items);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(espnow_get_peer_obj, espnow_get_peer);

// ESPNow.del_peer(peer_mac): Unregister peer_mac.
// Raise OSError if ESPNow.init() has not been called.
// Raise ValueError if peer is not a bytes-like objects or wrong length.
// Return None.
STATIC mp_obj_t espnow_del_peer(mp_obj_t _, mp_obj_t peer) {
    // TODO: Is this redundant?
    uint8_t peer_addr[ESP_NOW_ETH_ALEN];
    memcpy(peer_addr, _get_peer(peer), ESP_NOW_ETH_ALEN);

    check_esp_err(esp_now_del_peer(peer_addr));
    _update_peer_count();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(espnow_del_peer_obj, espnow_del_peer);

// ESPNow.get_peers(): Fetch peer_info records for all registered ESPNow peers.
// Raise OSError if ESPNow.init() has not been called.
// Return a tuple of tuples:
//     ((peer_addr, lmk, channel, ifidx, encrypt),
//      (peer_addr, lmk, channel, ifidx, encrypt), ...)
STATIC mp_obj_t espnow_get_peers(mp_obj_t _) {
    esp_espnow_obj_t *self = get_singleton(INITIALISED);

    mp_obj_tuple_t *peerinfo_tuple = mp_obj_new_tuple(self->peer_count, NULL);
    esp_now_peer_info_t peer = {0};
    bool from_head = true;
    int count = 0;
    while (esp_now_fetch_peer(from_head, &peer) == ESP_OK) {
        from_head = false;
        mp_obj_t items[] = {
            mp_obj_new_bytes(peer.peer_addr, sizeof(peer.peer_addr)),
            mp_obj_new_bytes(peer.lmk, sizeof(peer.lmk)),
            mp_obj_new_int(peer.channel),
            mp_obj_new_int(peer.ifidx),
            (peer.encrypt) ? mp_const_true : mp_const_false,
        };
        peerinfo_tuple->items[count] = mp_obj_new_tuple(5, items);
        if (++count >= self->peer_count) {
            break;          // Should not happen
        }
    }

    return peerinfo_tuple;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(espnow_get_peers_obj, espnow_get_peers);

// ESPNow.espnow_peer_count(): Get the number of registered peers.
// Raise OSError if ESPNow.init() has not been called.
// Return a tuple of (num_total_peers, num_encrypted_peers).
STATIC mp_obj_t espnow_peer_count(mp_obj_t _) {
    esp_now_peer_num_t peer_num = {0};
    check_esp_err(esp_now_get_peer_num(&peer_num));

    mp_obj_t items[] = {
        mp_obj_new_int(peer_num.total_num),
        mp_obj_new_int(peer_num.encrypt_num),
    };
    return mp_obj_new_tuple(2, items);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(espnow_peer_count_obj, espnow_peer_count);

// ### Stream I/O protocol functions (to support uasyncio)
//

// Read an ESPNow packet into a stream buffer
STATIC mp_uint_t espnow_stream_read(mp_obj_t self_in, void *buf_in,
    mp_uint_t size, int *errcode) {
    esp_espnow_obj_t *self = get_singleton(INITIALISED);

    int len = _get_packet(self->recv_buffer, buf_in, size, 0);
    if (len == 0) {
        *errcode = MP_EAGAIN;
        return MP_STREAM_ERROR;
    }
    return len;
}

// Adapted from py/stream.c:stream_readinto()
// Want to force just a single read - don't keep looping to fill the buffer.
STATIC mp_obj_t espnow_stream_readinto1(size_t n_args, const mp_obj_t *args) {
    esp_espnow_obj_t *self = get_singleton(INITIALISED);

    mp_buffer_info_t buf;
    mp_get_buffer_raise(args[1], &buf, MP_BUFFER_WRITE);

    int len = _get_packet(self->recv_buffer, buf.buf, buf.len, 0);

    return (len > 0) ? MP_OBJ_NEW_SMALL_INT(len) : mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    espnow_stream_readinto1_obj, 2, 3, espnow_stream_readinto1);

// ESPNow.write(packet): Send a message from an ESPNow packet in buf_in.
// Raise OSError if not initialised (ESPNow.init()).
// Raise ValueError if there is an ee
STATIC mp_uint_t espnow_stream_write(mp_obj_t self_in, const void *buf_in,
    mp_uint_t max_size, int *errcode) {
    esp_espnow_obj_t *self = get_singleton(INITIALISED);
    espnow_pkt_t *pkt = (espnow_pkt_t *)buf_in;

    int pkt_len = _check_packet_length((espnow_hdr_t *)pkt, max_size);
    // Send the message to the peer
    _do_espnow_send(self, pkt->peer, pkt->msg, pkt->msg_len, false);
    return pkt_len;
}

// Support MP_STREAM_POLL for asyncio
STATIC mp_uint_t espnow_stream_ioctl(mp_obj_t self_in, mp_uint_t request,
    mp_uint_t arg, int *errcode) {
    esp_espnow_obj_t *self = get_singleton(INITIALISED);
    mp_uint_t ret;
    if (request == MP_STREAM_POLL) {
        mp_uint_t flags = arg;
        ret = 0;
        // Consider read ready when the incoming buffer is not empty
        if ((flags & MP_STREAM_POLL_RD) && !buffer_empty(self->recv_buffer)) {
            ret |= MP_STREAM_POLL_RD;
        }
        // Consider write ready when all sent packets have been acknowledged.
        if ((flags & MP_STREAM_POLL_WR) &&
            self->tx_responses >= self->tx_packets) {
            ret |= MP_STREAM_POLL_WR;
        }
    } else {
        *errcode = MP_EINVAL;
        ret = MP_STREAM_ERROR;
    }
    return ret;
}

// Iterating over ESPNow returns tuples of (peer_addr, message)...
STATIC mp_obj_t espnow_iternext(mp_obj_t self_in) {
    esp_espnow_obj_t *self = get_singleton(0);
    if (self->recv_buffer == NULL) {
        return MP_OBJ_STOP_ITERATION;
    }
    return espnow_irecv(1, &self_in);
}

STATIC void espnow_print(const mp_print_t *print, mp_obj_t self_in,
    mp_print_kind_t kind) {
    esp_espnow_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "ESPNow(rxbuf=%u, timeout=%u)",
        self->recv_buffer_size, self->recv_timeout_ms);
}

STATIC const mp_rom_map_elem_t esp_espnow_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&espnow_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&espnow_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_config), MP_ROM_PTR(&espnow_config_obj) },
    { MP_ROM_QSTR(MP_QSTR_stats), MP_ROM_PTR(&espnow_stats_obj) },
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&espnow_version_obj) },

    // Send and receive messages
    { MP_ROM_QSTR(MP_QSTR_recv), MP_ROM_PTR(&espnow_recv_obj) },
    { MP_ROM_QSTR(MP_QSTR_irecv), MP_ROM_PTR(&espnow_irecv_obj) },
    { MP_ROM_QSTR(MP_QSTR_send), MP_ROM_PTR(&espnow_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&espnow_poll_obj) },

    // Peer management functions
    { MP_ROM_QSTR(MP_QSTR_set_pmk), MP_ROM_PTR(&espnow_set_pmk_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_peer), MP_ROM_PTR(&espnow_add_peer_obj) },
    { MP_ROM_QSTR(MP_QSTR_mod_peer), MP_ROM_PTR(&espnow_mod_peer_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_peer), MP_ROM_PTR(&espnow_get_peer_obj) },
    { MP_ROM_QSTR(MP_QSTR_del_peer), MP_ROM_PTR(&espnow_del_peer_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_peers), MP_ROM_PTR(&espnow_get_peers_obj) },
    { MP_ROM_QSTR(MP_QSTR_peer_count), MP_ROM_PTR(&espnow_peer_count_obj) },

    // StreamIO and uasyncio support
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_read1), MP_ROM_PTR(&mp_stream_read1_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto1), MP_ROM_PTR(&espnow_stream_readinto1_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
};
STATIC MP_DEFINE_CONST_DICT(esp_espnow_locals_dict, esp_espnow_locals_dict_table);

STATIC const mp_rom_map_elem_t espnow_globals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_espnow) },
    { MP_ROM_QSTR(MP_QSTR_ESPNow), MP_ROM_PTR(&esp_espnow_type) },
    { MP_ROM_QSTR(MP_QSTR_MAX_DATA_LEN), MP_ROM_INT(ESP_NOW_MAX_DATA_LEN)},
    { MP_ROM_QSTR(MP_QSTR_KEY_LEN), MP_ROM_INT(ESP_NOW_KEY_LEN)},
    { MP_ROM_QSTR(MP_QSTR_MAX_TOTAL_PEER_NUM), MP_ROM_INT(ESP_NOW_MAX_TOTAL_PEER_NUM)},
    { MP_ROM_QSTR(MP_QSTR_MAX_ENCRYPT_PEER_NUM), MP_ROM_INT(ESP_NOW_MAX_ENCRYPT_PEER_NUM)},
};
STATIC MP_DEFINE_CONST_DICT(espnow_globals_dict, espnow_globals_dict_table);

STATIC const mp_stream_p_t espnow_stream_p = {
    .read = espnow_stream_read,
    .write = espnow_stream_write,
    .ioctl = espnow_stream_ioctl,
    .is_text = false,
};

const mp_obj_type_t esp_espnow_type = {
    { &mp_type_type },
    .name = MP_QSTR_ESPNow,
    .make_new = espnow_make_new,
    .print = espnow_print,
    .getiter = mp_identity_getiter,
    .iternext = espnow_iternext,
    .protocol = &espnow_stream_p,
    .locals_dict = (mp_obj_t)&esp_espnow_locals_dict,
};

const mp_obj_module_t mp_module_esp_espnow = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&espnow_globals_dict,
};

MP_REGISTER_ROOT_POINTER(struct _esp_espnow_obj_t *espnow_singleton);