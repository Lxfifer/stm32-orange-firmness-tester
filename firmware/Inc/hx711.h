/**
  * @file    hx711.h
  * @brief   HX711 24-bit load-cell ADC driver (STM32 HAL version).
  *          Ported from the original load-cell project (StdPeriph version).
  * Wiring : SCK -> PC14, DT -> PC13
  * Note   : PC13/PC14 are in the backup domain of the STM32F103. VBAT must be
  *          connected to 3.3 V (or to a battery), otherwise these pins do not
  *          work and the weight reading is always 0.
  */
#ifndef __HX711_H
#define __HX711_H

#include "main.h"

/* ========== HX711 pin assignment ========== */
#define HX711_SCK_PORT      GPIOC
#define HX711_SCK_PIN       GPIO_PIN_14   /* PC14 -> SCK */
#define HX711_DT_PORT       GPIOC
#define HX711_DT_PIN        GPIO_PIN_13   /* PC13 -> DT  */

/* ========== API ========== */
void    HX711_GPIO_Init(void);      /* init the HX711 GPIOs and the DWT delay */
uint32_t Read_HX711(void);          /* one raw 24-bit sample (channel A, gain 128), 0 on timeout */
void    HX711_GetTare(void);        /* no-load tare: store the zero point */
float   HX711_GetWeight(void);      /* weight in grams, median-filtered, tare-corrected, ~10 Hz */
uint8_t HX711_LastOk(void);         /* 1 = last raw read succeeded (DOUT answered), 0 = timeout */
uint8_t HX711_TareHadEnough(void);  /* 1 = last tare collected enough valid samples */

#endif
