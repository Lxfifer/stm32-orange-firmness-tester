/**
  * @file    scale_module.h
  * @brief   Digital load-cell transmitter module (custom TTL protocol), HAL port
  *          on USART3.
  * Based on: the transmitter-module sample and the load-cell reference project
  *           (standard-peripheral version).
  *
  * Wiring (this HAL orange-press project):
  *   module VCC -> 3.3 V / 5 V, GND common
  *   module RXD <- STM32 PB10 (USART3_TX)
  *   module TXD -> STM32 PB11 (USART3_RX)
  *   Note: the reference project used USART2 (PA2/PA3), but PA2/PA3 are taken
  *         by the stepper EN/DIR lines, hence USART3 here.
  *
  * Protocol (9600 8N1, hex frames, XOR checksum):
  *   read weight: send {A3 00 A2 A4 A5} -> receive 10 bytes AA A3 00 ... FF
  *   tare       : send {AB 00 AA AC AD} -> receive 10 bytes (module needs ~600 ms)
  *   parameters : F2 read / FB write (20-byte frames)
  * The returned weight is the module's internal "net count";
  * grams = count * 0.00979 (calibrated in the reference project).
  *
  * ARCHIVED: alternative weighing front end, not part of the current build.
  */
#ifndef __SCALE_MODULE_H
#define __SCALE_MODULE_H

#include "main.h"

#define SM_BAUD            9600u   /* module baud rate (factory default) */
#define SM_ADDR            0x00u   /* module address (default 0) */

/* Module command bytes */
#define SM_CMD_READ_AD     0xA1u
#define SM_CMD_READ_W      0xA3u   /* read weight (most used) */
#define SM_CMD_ZERO        0xAAu
#define SM_CMD_TARE        0xABu   /* tare (temporary zero, lost on power-down) */
#define SM_CMD_UNTARE      0xACu
#define SM_CMD_CAL_WEIGHT  0xADu
#define SM_CMD_AUTO_SEND   0xA4u
#define SM_CMD_READ_PARAM  0xF2u
#define SM_CMD_WRITE_PARAM 0xFBu

#define SM_AD_RATE_10HZ    10u
#define SM_AD_RATE_40HZ    40u

/* AD2.0 module "freq" codes (manual appendix C): 1=10Hz 2=40Hz 3=640Hz 4=1280Hz */
#define SM_FREQ_640HZ      3u
#define SM_FREQ_1280HZ     4u
/* Baud-rate index: 1=2400 2=4800 3=9600 4=19200 5=28800 6=38400 7=57600 8=115200 */

/* ===== Target parameters (written and read back at start-up) ===== */
#define MOD_FREQ           SM_FREQ_640HZ   /* 3 = 640 Hz sampling */
#define MOD_AVG            1u              /* averaging = 1 (fastest response, needed
                                                for contact detection; at 640 Hz the
                                                lag is tiny. Raise to 3..10 if noisy) */
#define MOD_BAUD_IDX       8u              /* 8 = 115200 */
#define SM_BAUD_NEW        115200u         /* target link baud rate */
/* Start-up first tries this baud rate (the module may sit at 9600 or 115200) */
#define SM_BAUD_FIRST      9600u

/* Module parameters (F2/FB) */
typedef struct {
    uint8_t addr;        /* address                                */
    uint8_t freq;        /* AD rate 10/40 (1..4 on AD2.0 hardware)  */
    uint8_t div;         /* division 1/2/5/10/20/50/100             */
    uint8_t baud;        /* baud index 1=2400..8=115200, 3=9600     */
    uint8_t zeroRange;   /* zero-tracking range 0..50               */
    uint8_t zeroEn;      /* power-on zero + dynamic tracking 0..3   */
    uint8_t median;      /* median filter 1/3/5/7/9                 */
    uint8_t avg;         /* averaging filter 1..50                  */
    uint8_t track;       /* dynamic tracking range 0..50            */
    uint8_t creep;       /* creep compensation 0..10                */
    uint8_t stable;      /* stable-output switch / stability bit    */
    uint8_t zeroStr;     /* zero strength 1..20                     */
} SM_Param;

/* Grams per count (calibrated in the reference project: 203.7 g -> count 20807) */
#define SM_G_PER_COUNT  0.00979f

void SM_UartInit(uint32_t baud);              /* init USART3 (PB10/PB11) + RX interrupt */
void SM_SendBytes(const uint8_t *buf, uint16_t len);
void SM_FlushRx(void);
void SM_SetBaud(uint32_t baud);               /* switch the link baud rate (after a full frame) */
void SM_SendParamsRaw(const SM_Param *p);     /* send an FB write frame (no reply expected) */

int  SM_ReadParams(SM_Param *p);              /* F2: 1 = ok */
int  SM_WriteParams(const SM_Param *p);       /* FB: 1 = ok (same baud rate) */
int  SM_Tare(int32_t *net);                   /* AB tare, the module needs about 600 ms */
int  SM_StartRead(void);                      /* send an A3 weight request */
int  SM_PollWeight(int32_t *w, uint16_t waitMs); /* wait/scan for the A3 reply */

#endif
