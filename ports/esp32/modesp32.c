/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 "Eric Poulsen" <eric@zyxod.com>
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
#include <string.h>

#include <time.h>
#include <sys/time.h>
#include "soc/rtc_cntl_reg.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_heap_caps.h"
#include "multi_heap.h"
#include "esp_sleep.h"

#include "py/nlr.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "shared/timeutils/timeutils.h"
#include "modmachine.h"
#include "machine_rtc.h"
#include "modesp32.h"

// These private includes are needed for idf_heap_info.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 3, 0)
#define MULTI_HEAP_FREERTOS
#include "../multi_heap_platform.h"
#endif
#include "../heap_private.h"

STATIC mp_obj_t esp32_wake_on_touch(const mp_obj_t wake) {

    if (machine_rtc_config.ext0_pin != -1) {
        mp_raise_ValueError(MP_ERROR_TEXT("no resources"));
    }
    // mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("touchpad wakeup not available for this version of ESP-IDF"));

    machine_rtc_config.wake_on_touch = mp_obj_is_true(wake);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_wake_on_touch_obj, esp32_wake_on_touch);

STATIC mp_obj_t esp32_wake_on_ext0(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    if (machine_rtc_config.wake_on_touch) {
        mp_raise_ValueError(MP_ERROR_TEXT("no resources"));
    }
    enum {ARG_pin, ARG_level};
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_pin,  MP_ARG_OBJ, {.u_obj = mp_obj_new_int(machine_rtc_config.ext0_pin)} },
        { MP_QSTR_level,  MP_ARG_BOOL, {.u_bool = machine_rtc_config.ext0_level} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_pin].u_obj == mp_const_none) {
        machine_rtc_config.ext0_pin = -1; // "None"
    } else {
        gpio_num_t pin_id = machine_pin_get_id(args[ARG_pin].u_obj);
        if (pin_id != machine_rtc_config.ext0_pin) {
            if (!RTC_IS_VALID_EXT_PIN(pin_id)) {
                mp_raise_ValueError(MP_ERROR_TEXT("invalid pin"));
            }
            machine_rtc_config.ext0_pin = pin_id;
        }
    }

    machine_rtc_config.ext0_level = args[ARG_level].u_bool;
    machine_rtc_config.ext0_wake_types = MACHINE_WAKE_SLEEP | MACHINE_WAKE_DEEPSLEEP;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(esp32_wake_on_ext0_obj, 0, esp32_wake_on_ext0);

STATIC mp_obj_t esp32_wake_on_ext1(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum {ARG_pins, ARG_level};
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_pins, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_level, MP_ARG_BOOL, {.u_bool = machine_rtc_config.ext1_level} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    uint64_t ext1_pins = machine_rtc_config.ext1_pins;


    // Check that all pins are allowed
    if (args[ARG_pins].u_obj != mp_const_none) {
        size_t len = 0;
        mp_obj_t *elem;
        mp_obj_get_array(args[ARG_pins].u_obj, &len, &elem);
        ext1_pins = 0;

        for (int i = 0; i < len; i++) {

            gpio_num_t pin_id = machine_pin_get_id(elem[i]);
            if (!RTC_IS_VALID_EXT_PIN(pin_id)) {
                mp_raise_ValueError(MP_ERROR_TEXT("invalid pin"));
                break;
            }
            ext1_pins |= (1ll << pin_id);
        }
    }

    machine_rtc_config.ext1_level = args[ARG_level].u_bool;
    machine_rtc_config.ext1_pins = ext1_pins;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(esp32_wake_on_ext1_obj, 0, esp32_wake_on_ext1);

STATIC mp_obj_t esp32_wake_on_ulp(const mp_obj_t wake) {
    if (machine_rtc_config.ext0_pin != -1) {
        mp_raise_ValueError(MP_ERROR_TEXT("no resources"));
    }
    machine_rtc_config.wake_on_ulp = mp_obj_is_true(wake);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_wake_on_ulp_obj, esp32_wake_on_ulp);

STATIC mp_obj_t esp32_gpio_deep_sleep_hold(const mp_obj_t enable) {
    if (mp_obj_is_true(enable)) {
        gpio_deep_sleep_hold_en();
    } else {
        gpio_deep_sleep_hold_dis();
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_gpio_deep_sleep_hold_obj, esp32_gpio_deep_sleep_hold);

#if CONFIG_IDF_TARGET_ESP32

#include "soc/sens_reg.h"

STATIC mp_obj_t esp32_raw_temperature(void) {
    SET_PERI_REG_BITS(SENS_SAR_MEAS_WAIT2_REG, SENS_FORCE_XPD_SAR, 3, SENS_FORCE_XPD_SAR_S);
    SET_PERI_REG_BITS(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_CLK_DIV, 10, SENS_TSENS_CLK_DIV_S);
    CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
    CLEAR_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
    SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP_FORCE);
    SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_POWER_UP);
    ets_delay_us(100);
    SET_PERI_REG_MASK(SENS_SAR_TSENS_CTRL_REG, SENS_TSENS_DUMP_OUT);
    ets_delay_us(5);
    int res = GET_PERI_REG_BITS2(SENS_SAR_SLAVE_ADDR3_REG, SENS_TSENS_OUT, SENS_TSENS_OUT_S);

    return mp_obj_new_int(res);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(esp32_raw_temperature_obj, esp32_raw_temperature);

STATIC mp_obj_t esp32_hall_sensor(void) {
    adc1_config_width(ADC_WIDTH_12Bit);
    return MP_OBJ_NEW_SMALL_INT(hall_sensor_read());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(esp32_hall_sensor_obj, esp32_hall_sensor);

#endif

STATIC mp_obj_t esp32_idf_heap_info(const mp_obj_t cap_in) {
    mp_int_t cap = mp_obj_get_int(cap_in);
    multi_heap_info_t info;
    heap_t *heap;
    mp_obj_t heap_list = mp_obj_new_list(0, 0);
    SLIST_FOREACH(heap, &registered_heaps, next) {
        if (heap_caps_match(heap, cap)) {
            multi_heap_get_info(heap->heap, &info);
            mp_obj_t data[] = {
                MP_OBJ_NEW_SMALL_INT(heap->end - heap->start), // total heap size
                MP_OBJ_NEW_SMALL_INT(info.total_free_bytes),   // total free bytes
                MP_OBJ_NEW_SMALL_INT(info.largest_free_block), // largest free contiguous
                MP_OBJ_NEW_SMALL_INT(info.minimum_free_bytes), // minimum free seen
            };
            mp_obj_t this_heap = mp_obj_new_tuple(4, data);
            mp_obj_list_append(heap_list, this_heap);
        }
    }
    return heap_list;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(esp32_idf_heap_info_obj, esp32_idf_heap_info);

// @glenn20: Support for tracing boot execution times for micropython.
// Calls to esp32_boot_trace() will:
//  - Issue a 100 microsecond pulse on BOOT_TRACE_PIN (pin 18) and
//  - record the number of microseconds since boot in an internal array
// This will add 200 microseconds delay for each call to esp32_boot_trace().
//
// The boot times array (in microseconds) may be accessed from micropython with:
//
//    import esp32
//    print(esp32.boot_times())
//

#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/rtc.h"
#endif
#include "hal/rtc_cntl_ll.h"
#include "soc/timer_group_reg.h"

// deepsleep_for_us() and esp_wake_deep_sleep() are executed before loading the
// whole firmware image and can only call functions which are stored in ROM so,
// we have to use the low-level register programming of the esp32XX devices. See
// https://gist.github.com/igrr/54f7fbe0513ac14e1aea3fd7fbecfeab

RTC_IRAM_ATTR void deepsleep_for_ms(uint32_t duration_us, uint32_t wake_mask) {
    // Feed the system watchdog timer
    REG_WRITE(TIMG_WDTFEED_REG(0), 1);

    // Get current RTC time
    SET_PERI_REG_MASK(RTC_CNTL_TIME_UPDATE_REG, RTC_CNTL_TIME_UPDATE);
    #if CONFIG_IDF_TARGET_ESP32
    while (GET_PERI_REG_MASK(RTC_CNTL_TIME_UPDATE_REG, RTC_CNTL_TIME_VALID) == 0) {
        ets_delay_us(1);
    }
    SET_PERI_REG_MASK(RTC_CNTL_INT_CLR_REG, RTC_CNTL_TIME_VALID_INT_CLR);
    #endif
    uint64_t now = READ_PERI_REG(RTC_CNTL_TIME0_REG);
    now |= ((uint64_t) READ_PERI_REG(RTC_CNTL_TIME1_REG)) << 32;

    // Use the RTC calibration values
    uint32_t period = REG_READ(RTC_SLOW_CLK_CAL_REG);
    uint64_t rtc_count_delta = ((((uint64_t)duration_us) << RTC_CLK_CAL_FRACT) / period);

    // Set the wakeup time - can be called as it is static inline (not in flash)
    rtc_cntl_ll_set_wakeup_timer(now + rtc_count_delta);

    // Enable wake from the RTC timer
    REG_SET_FIELD(RTC_CNTL_WAKEUP_STATE_REG, RTC_CNTL_WAKEUP_ENA, RTC_TIMER_TRIG_EN | wake_mask);
    // ??? Is this necessary ???
    WRITE_PERI_REG(RTC_CNTL_SLP_REJECT_CONF_REG, 0); // Clear sleep rejection cause

    // Set the wake stub so we execute on the next deepsleep wake.
    // The ESP32-S3/C3 devices need special handling.
    // From https://github.com/espressif/esp-idf/issues/8208#issuecomment-1110764199
    #if SOC_PM_SUPPORT_DEEPSLEEP_VERIFY_STUB_ONLY
    extern char _rtc_text_start[];
    #if CONFIG_ESP32S3_RTCDATA_IN_FAST_MEM
    extern char _rtc_noinit_end[];
    size_t rtc_fast_length = (size_t)_rtc_noinit_end - (size_t)_rtc_text_start;
    #else
    extern char _rtc_force_fast_end[];
    size_t rtc_fast_length = (size_t)_rtc_force_fast_end - (size_t)_rtc_text_start;
    #endif // CONFIG_ESP32S3_RTCDATA_IN_FAST_MEM
    // Please note, the entry address of stub code is a fixed address at `_rtc_text_start`.
    esp_rom_set_rtc_wake_addr((esp_rom_wake_func_t)_rtc_text_start, rtc_fast_length);
    #else
    // Set the pointer of the wake stub function.
    REG_WRITE(RTC_ENTRY_ADDR_REG, (uint32_t)&esp_wake_deep_sleep);
    set_rtc_memory_crc();
    #endif // SOC_PM_SUPPORT_DEEPSLEEP_CHECK_STUB_MEM

    // Go to sleep
    CLEAR_PERI_REG_MASK(RTC_CNTL_STATE0_REG, RTC_CNTL_SLEEP_EN);
    SET_PERI_REG_MASK(RTC_CNTL_STATE0_REG, RTC_CNTL_SLEEP_EN);
    // A few CPU cycles may be necessary for the sleep to start...
    while (true) {
        ;
    }
    // never reaches here.
}

RTC_DATA_ATTR uint32_t deepsleep_wake_count = 0;
RTC_DATA_ATTR uint32_t deepsleep_wake_count_period = 5;
RTC_DATA_ATTR uint32_t deepsleep_wake_timeout_us = 500 * 1000;

#ifdef CONFIG_IDF_TARGET_ESP32
#define WAKE_CAUSE_REG RTC_CNTL_WAKEUP_STATE_REG
#else
#define WAKE_CAUSE_REG RTC_CNTL_SLP_WAKEUP_CAUSE_REG
#endif // CONFIG_IDF_TARGET_ESP32


void RTC_IRAM_ATTR esp_wake_deep_sleep(void) {
    esp_default_wake_deep_sleep();
    // uint32_t wakeup_cause = REG_GET_FIELD(WAKE_CAUSE_REG, RTC_CNTL_WAKEUP_CAUSE);
    // if (wakeup_cause & RTC_TIMER_TRIG_EN) {
    // } else if (wakeup_cause & RTC_EXT0_TRIG_EN) {
    //     return;
    // } else if (wakeup_cause & RTC_EXT1_TRIG_EN) {
    //     return;
    // }
    if ((++deepsleep_wake_count % deepsleep_wake_count_period) == 0) {
        // Do a full boot-up every deepsleep_wake_count_period times.
        return;
    }
    deepsleep_for_ms(deepsleep_wake_timeout_us, 0);
}

static void busy_wait_us(uint32_t us) {
    uint64_t t0 = esp_timer_get_time();
    for (;;) {
        uint64_t dt = esp_timer_get_time() - t0;
        if (dt >= us) {
            return;
        }
    }
}

#define BOOT_TRACE_PIN 18
uint32_t boot_times[20];
static int ntimes = 0;

void esp32_boot_trace() {
    if (ntimes == 0) {
        gpio_pad_select_gpio(BOOT_TRACE_PIN);
        gpio_set_level(BOOT_TRACE_PIN, 0);
        gpio_set_direction(BOOT_TRACE_PIN, GPIO_MODE_OUTPUT);
        gpio_set_level(BOOT_TRACE_PIN, 0);
        busy_wait_us(1000);
    }
    if (ntimes < sizeof(boot_times)/sizeof(boot_times[0])) {
        boot_times[ntimes++] = esp_timer_get_time();
    }
    gpio_set_level(BOOT_TRACE_PIN, 1);
    busy_wait_us(100);
    gpio_set_level(BOOT_TRACE_PIN, 0);
    busy_wait_us(100);
}

STATIC mp_obj_t esp32_boot_times() {
    mp_obj_tuple_t *tuple = mp_obj_new_tuple(ntimes, NULL);
    for (int i = 0; i < ntimes; i++) {
        tuple->items[i] = mp_obj_new_int(boot_times[i]);
    }
    return MP_OBJ_FROM_PTR(tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(esp32_boot_times_obj, esp32_boot_times);

STATIC const mp_rom_map_elem_t esp32_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_esp32) },

    { MP_ROM_QSTR(MP_QSTR_wake_on_touch), MP_ROM_PTR(&esp32_wake_on_touch_obj) },
    { MP_ROM_QSTR(MP_QSTR_wake_on_ext0), MP_ROM_PTR(&esp32_wake_on_ext0_obj) },
    { MP_ROM_QSTR(MP_QSTR_wake_on_ext1), MP_ROM_PTR(&esp32_wake_on_ext1_obj) },
    { MP_ROM_QSTR(MP_QSTR_wake_on_ulp), MP_ROM_PTR(&esp32_wake_on_ulp_obj) },
    { MP_ROM_QSTR(MP_QSTR_gpio_deep_sleep_hold), MP_ROM_PTR(&esp32_gpio_deep_sleep_hold_obj) },
    #if CONFIG_IDF_TARGET_ESP32
    { MP_ROM_QSTR(MP_QSTR_raw_temperature), MP_ROM_PTR(&esp32_raw_temperature_obj) },
    { MP_ROM_QSTR(MP_QSTR_hall_sensor), MP_ROM_PTR(&esp32_hall_sensor_obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_idf_heap_info), MP_ROM_PTR(&esp32_idf_heap_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_boot_times), MP_ROM_PTR(&esp32_boot_times_obj) },

    { MP_ROM_QSTR(MP_QSTR_NVS), MP_ROM_PTR(&esp32_nvs_type) },
    { MP_ROM_QSTR(MP_QSTR_Partition), MP_ROM_PTR(&esp32_partition_type) },
    { MP_ROM_QSTR(MP_QSTR_RMT), MP_ROM_PTR(&esp32_rmt_type) },
    #if CONFIG_IDF_TARGET_ESP32
    { MP_ROM_QSTR(MP_QSTR_ULP), MP_ROM_PTR(&esp32_ulp_type) },
    #endif

    { MP_ROM_QSTR(MP_QSTR_WAKEUP_ALL_LOW), MP_ROM_FALSE },
    { MP_ROM_QSTR(MP_QSTR_WAKEUP_ANY_HIGH), MP_ROM_TRUE },

    { MP_ROM_QSTR(MP_QSTR_HEAP_DATA), MP_ROM_INT(MALLOC_CAP_8BIT) },
    { MP_ROM_QSTR(MP_QSTR_HEAP_EXEC), MP_ROM_INT(MALLOC_CAP_EXEC) },
};

STATIC MP_DEFINE_CONST_DICT(esp32_module_globals, esp32_module_globals_table);

const mp_obj_module_t esp32_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&esp32_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_esp32, esp32_module);
