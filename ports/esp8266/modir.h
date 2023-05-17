/*
 * ir_remote_def.h
 *
 * Version 1.1, 08.03.2016
 * Written by Valeriy Kucherenko
 * For details, see https://github.com/valkuc/esp8266-ir-remote
 *
 * IR code defines are based on https://github.com/shirriff/Arduino-IRremote by Ken Shirriff
 */

#ifndef __IR_REMOTE_DEF_H__
#define __IR_REMOTE_DEF_H__

#define TOPBIT 0x80000000

#define NEC_FREQUENCY 38400
#define NEC_HDR_MARK 9000
#define NEC_HDR_SPACE 4500
#define NEC_BIT_MARK 562
#define NEC_ONE_SPACE 1687
#define NEC_ZERO_SPACE 562

#define CLOCK_DIV_1 0
#define CLOCK_DIV_16 4
#define CLOCK_DIV_256 8

#define TM_LEVEL_INT 1
#define TM_EDGE_INT 0

#define AUTO_RELOAD_CNT_TIMER BIT6
#define FRC1_ENABLE_TIMER BIT7

#define FREQ_TO_TICKS(x) \
        (((APB_CLK_FREQ >> CLOCK_DIV_1) / (x) * (1000 / 2)) / 1000)

#endif
