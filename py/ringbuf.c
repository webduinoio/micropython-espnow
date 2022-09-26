/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Jim Mussared
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
#include "ringbuf.h"

int ringbuf_get16(ringbuf_t *r) {
    int v = ringbuf_peek16(r);
    if (v == -1) {
        return v;
    }
    r->iget += 2;
    if (r->iget >= r->size) {
        r->iget -= r->size;
    }
    return v;
}

int ringbuf_peek16(ringbuf_t *r) {
    if (r->iget == r->iput) {
        return -1;
    }
    uint32_t iget_a = r->iget + 1;
    if (iget_a == r->size) {
        iget_a = 0;
    }
    if (iget_a == r->iput) {
        return -1;
    }
    return (r->buf[r->iget] << 8) | (r->buf[iget_a]);
}

int ringbuf_put16(ringbuf_t *r, uint16_t v) {
    uint32_t iput_a = r->iput + 1;
    if (iput_a == r->size) {
        iput_a = 0;
    }
    if (iput_a == r->iget) {
        return -1;
    }
    uint32_t iput_b = iput_a + 1;
    if (iput_b == r->size) {
        iput_b = 0;
    }
    if (iput_b == r->iget) {
        return -1;
    }
    r->buf[r->iput] = (v >> 8) & 0xff;
    r->buf[iput_a] = v & 0xff;
    r->iput = iput_b;
    return 0;
}

#if MICROPY_PY_MICROPYTHON_RINGBUFFER

#include "py/runtime.h"
#include "py/stream.h"
#include "py/mphal.h"

typedef struct _micropython_ringbuffer_obj_t {
    mp_obj_base_t base;
    ringbuf_t ringbuffer;
    uint16_t timeout;       // timeout waiting for first char (in ms)
} micropython_ringbuffer_obj_t;

STATIC mp_obj_t micropython_ringbuffer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 2, false);
    mp_int_t buff_size = -1;
    mp_buffer_info_t bufinfo = {NULL, 0, 0};

    if (!mp_get_buffer(args[0], &bufinfo, MP_BUFFER_RW)) {
        buff_size = mp_obj_get_int(args[0]);
    }
    micropython_ringbuffer_obj_t *self = mp_obj_malloc(micropython_ringbuffer_obj_t, type);
    if (bufinfo.buf != NULL) {
        // buffer passed in, use it directly for ringbuffer
        self->ringbuffer.buf = bufinfo.buf;
        self->ringbuffer.size = bufinfo.len;
        self->ringbuffer.iget = self->ringbuffer.iput = 0;
    } else {
        // Allocation buffer, sdd one extra to buff_size as ringbuf consumes one byte for tracking.
        ringbuf_alloc(&(self->ringbuffer), buff_size + 1);
    }

    if (n_args > 1) {
        self->timeout = mp_obj_get_int(args[1]);
    }
    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t micropython_ringbuffer_settimeout(mp_obj_t self_in, mp_obj_t timeout_in) {
    micropython_ringbuffer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    self->timeout = mp_obj_get_int(timeout_in);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(micropython_ringbuffer_settimeout_obj, micropython_ringbuffer_settimeout);


STATIC mp_uint_t micropython_ringbuffer_read(mp_obj_t self_in, void *buf_in, mp_uint_t size, int *errcode) {
    micropython_ringbuffer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t t = mp_hal_ticks_ms() + self->timeout;
    uint8_t *dest = buf_in;

    for (size_t i = 0; i < size; i++) {
        // Wait for the first/next character.
        while (ringbuf_avail(&self->ringbuffer) == 0) {
            if (mp_hal_ticks_ms() > t) {  // timed out
                if (i <= 0) {
                    *errcode = MP_EAGAIN;
                    return MP_STREAM_ERROR;
                } else {
                    return i;
                }
            }
            MICROPY_EVENT_POLL_HOOK
        }
        *dest++ = ringbuf_get(&(self->ringbuffer));
        t = mp_hal_ticks_ms() + self->timeout;
    }
    return size;
}

STATIC mp_uint_t micropython_ringbuffer_write(mp_obj_t self_in, const void *buf_in, mp_uint_t size, int *errcode) {
    micropython_ringbuffer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t t = mp_hal_ticks_ms() + self->timeout;
    const uint8_t *src = buf_in;
    size_t i = 0;

    // Put as many bytes as possible into the transmit buffer.
    while (i < size && ringbuf_free(&(self->ringbuffer)) > 0) {
        ringbuf_put(&(self->ringbuffer), *src++);
        ++i;
    }
    // If ringbuf full, block until drained elsewhere (eg. irq) or timeout.
    while (i < size) {
        while (ringbuf_free(&(self->ringbuffer)) == 0) {
            if (mp_hal_ticks_ms() > t) {  // timed out
                if (i <= 0) {
                    *errcode = MP_EAGAIN;
                    return MP_STREAM_ERROR;
                } else {
                    return i;
                }
            }
            MICROPY_EVENT_POLL_HOOK
        }
        ringbuf_put(&(self->ringbuffer), *src++);
        ++i;
        t = mp_hal_ticks_ms() + self->timeout;
    }
    // Just in case the fifo was drained during refill of the ringbuf.
    return size;
}

STATIC mp_uint_t micropython_ringbuffer_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    micropython_ringbuffer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_uint_t ret;
    if (request == MP_STREAM_POLL) {
        ret = 0;
        if ((arg & MP_STREAM_POLL_RD) && ringbuf_avail(&self->ringbuffer) > 0) {
            ret |= MP_STREAM_POLL_RD;
        }
        if ((arg & MP_STREAM_POLL_WR) && ringbuf_free(&self->ringbuffer) > 0) {
            ret |= MP_STREAM_POLL_WR;
        }
    } else if (request == MP_STREAM_FLUSH) {
        // Should we wait here until empty / timeout?
        ret = 0;
    } else if (request == MP_STREAM_CLOSE) {
        // We don't want to reset head/tail pointers as there might
        // still be someone using it, eg. if ringbuffer is used instead of
        // a socket, a "writer" might call close before the "reader" is
        // finished.
        // Should we flush here though?
        ret = 0;
    } else {
        *errcode = MP_EINVAL;
        ret = MP_STREAM_ERROR;
    }
    return ret;
}

STATIC mp_obj_t micropython_ringbuffer_any(mp_obj_t self_in) {
    micropython_ringbuffer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(ringbuf_avail(&self->ringbuffer));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(micropython_ringbuffer_any_obj, micropython_ringbuffer_any);


STATIC const mp_rom_map_elem_t micropython_ringbuffer_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_any), MP_ROM_PTR(&micropython_ringbuffer_any_obj) },
    { MP_ROM_QSTR(MP_QSTR_settimeout), MP_ROM_PTR(&micropython_ringbuffer_settimeout_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&mp_stream_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },

};
STATIC MP_DEFINE_CONST_DICT(micropython_ringbuffer_locals_dict, micropython_ringbuffer_locals_dict_table);

STATIC const mp_stream_p_t ringbuffer_stream_p = {
    .read = micropython_ringbuffer_read,
    .write = micropython_ringbuffer_write,
    .ioctl = micropython_ringbuffer_ioctl,
    .is_text = false,
};

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_micropython_ringbuffer,
    MP_QSTR_ringbuffer,
    MP_TYPE_FLAG_NONE,
    make_new, micropython_ringbuffer_make_new,
    protocol, &ringbuffer_stream_p,
    locals_dict, &micropython_ringbuffer_locals_dict
    );

#endif
