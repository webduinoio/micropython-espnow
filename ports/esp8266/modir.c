/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
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

#include "modir.h"

#include <stdio.h>

#include "ets_sys.h"
#include "gpio.h"
#include "os_type.h"
#include "osapi.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"

static uint32_t _frc1_ticks;
static uint16_t _gpio_pin_num;

static bool _logic_high, _logic_low;
static bool _logic_low;

static volatile bool _pwm_lvl;

LOCAL os_timer_t timer;

static uint32_t initializing = false;
static uint32_t ir_data;

static void ICACHE_FLASH_ATTR set_carrier_frequence(uint16_t freq) {
    uint32_t ticks = FREQ_TO_TICKS(freq);
    if (_frc1_ticks != ticks) {
        _frc1_ticks = ticks;
        RTC_REG_WRITE(FRC1_LOAD_ADDRESS, _frc1_ticks);
    }
}
static void ICACHE_FLASH_ATTR mark(uint16_t time) {
    _pwm_lvl = _logic_high;
    GPIO_OUTPUT_SET(_gpio_pin_num, _pwm_lvl);

    TM1_EDGE_INT_ENABLE();
    ETS_FRC1_INTR_ENABLE();

    if (time > 0) {
        os_delay_us(time);
    }

    TM1_EDGE_INT_DISABLE();
    ETS_FRC1_INTR_DISABLE();
}

static void ICACHE_FLASH_ATTR space(uint16_t time) {
    _pwm_lvl = _logic_low;
    GPIO_OUTPUT_SET(_gpio_pin_num, _pwm_lvl);

    if (time > 0) {
        os_delay_us(time);
    }
}

LOCAL void pwm_tim1_intr_handler(void) {
    RTC_CLR_REG_MASK(FRC1_INT_ADDRESS, FRC1_INT_CLR_MASK);

    GPIO_OUTPUT_SET(_gpio_pin_num, _pwm_lvl);
    _pwm_lvl ^= 1;
}
void ICACHE_FLASH_ATTR ir_remote_init(uint32_t pin_mux, uint8_t pin_func,
    uint16_t pin_num,
    bool invert_logic_level) {
    if (initializing) {
        return;
    }
    _gpio_pin_num = pin_num;

    _logic_low = invert_logic_level;
    _logic_high = !_logic_low;
    _pwm_lvl = _logic_low;

    gpio_init();
    PIN_FUNC_SELECT(pin_mux, pin_func);
    GPIO_OUTPUT_SET(_gpio_pin_num, _logic_low);

    RTC_CLR_REG_MASK(FRC1_INT_ADDRESS, FRC1_INT_CLR_MASK);
    RTC_REG_WRITE(FRC1_CTRL_ADDRESS, CLOCK_DIV_1 | AUTO_RELOAD_CNT_TIMER |
        FRC1_ENABLE_TIMER | TM_EDGE_INT);
    RTC_REG_WRITE(FRC1_LOAD_ADDRESS, 0);

    ETS_FRC_TIMER1_INTR_ATTACH((ets_isr_t)pwm_tim1_intr_handler, NULL);
    initializing = true;
}
void ICACHE_FLASH_ATTR ir_remote_send_nec(uint32_t data, uint8_t nbits) {
    set_carrier_frequence(NEC_FREQUENCY);

    mark(NEC_HDR_MARK);
    space(NEC_HDR_SPACE);

    uint8_t i;
    for (i = 0; i < nbits; i++) {
        if (data & TOPBIT) {
            mark(NEC_BIT_MARK);
            space(NEC_ONE_SPACE);
        } else {
            mark(NEC_BIT_MARK);
            space(NEC_ZERO_SPACE);
        }
        data <<= 1;
    }
    mark(NEC_BIT_MARK);
    space(0);
}
LOCAL void ICACHE_FLASH_ATTR send_code_task(void *arg) {
    ir_remote_send_nec(ir_data, 32); // power on/off code for Yamaha RX-700
}

STATIC mp_obj_t init() {
    ir_remote_init(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2, 2, true);
    os_timer_disarm(&timer);
    os_timer_setfn(&timer, (os_timer_func_t *)send_code_task, (void *)0);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(init_obj, init);

STATIC mp_obj_t send_nec(mp_obj_t data) {
    ir_data = mp_obj_get_int(data);
    os_timer_arm(&timer, 1, 0);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(send_nec_obj, send_nec);

STATIC const mp_rom_map_elem_t ir_remote_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ir_remote)},
    {MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&init_obj)},
    {MP_ROM_QSTR(MP_QSTR_send_nec), MP_ROM_PTR(&send_nec_obj)},
};

STATIC MP_DEFINE_CONST_DICT(mp_module_ir_remote_globals,
    ir_remote_globals_table);

const mp_obj_module_t mp_module_ir_remote = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&mp_module_ir_remote_globals,
};

MP_REGISTER_MODULE(MP_QSTR_ir_remote, mp_module_ir_remote);
