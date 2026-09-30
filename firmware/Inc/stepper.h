#ifndef __STEPPER_H
#define __STEPPER_H

#include "main.h"

/* ========== HPD322 stepper-drive pin assignment ==========
 * The wiring is unchanged and keeps the original pin mapping:
 *   PA2 -> EN  (enable)
 *   PA3 -> DIR (direction)
 *   PA4 -> STEP/PUL (pulse)
 */
#define STEP_PORT       GPIOA
#define STEP_PIN        GPIO_PIN_4    /* PA4 -> PUL+  pulse */
#define DIR_PORT        GPIOA
#define DIR_PIN         GPIO_PIN_3    /* PA3 -> DIR+  direction */
#define ENABLE_PORT     GPIOA
#define ENABLE_PIN      GPIO_PIN_2    /* PA2 -> EN+   enable */

/* ========== Motor and slide parameters ==========
 * Motor: 42 mm stepper, 200 steps/rev (1.8 deg step angle)
 * Slide: T6x6-150 lead screw, 6 mm lead, 150 mm travel
 * HPD322: 16 microsteps by default (set with the DIP switches)
 * Steps per revolution = 200 * 16 = 3200
 */
#define MOTOR_STEPS_PER_REV    200    /* motor steps per revolution */
#define MICROSTEPS             16     /* HPD322 microstep setting (DIP switches) */
#define STEPS_PER_REV          (MOTOR_STEPS_PER_REV * MICROSTEPS)

/* ========== Speed control (multi-segment profile) ==========
 * Smooth ramping, updated every 100 ms.
 *
 * [Acceleration] distance > 80 mm
 *   acceleration: 10 RPM/100 ms = 100 RPM/s
 *   target speed: 1000 RPM
 *   pulse frequency = 1000/60 * 3200 = 53333 pps
 *   timer period    = 1,000,000 / 53333 = 18.75 us
 *
 * [Deceleration] 40 mm < distance <= 80 mm
 *   acceleration: 20 RPM/100 ms = 200 RPM/s
 *   target speed: 10 RPM
 *
 * [Forced low speed] distance <= 40 mm
 *   target speed: 10 RPM (forced)
 *
 * [Return] triggered below 30 mm
 *   reverse speed: 50 RPM
 *
 * Note: the HPD322 accepts up to 400 kHz; 1000 RPM is 53.3 kHz, far below it.
 */
#define FAST_RPM               1000   /* fast-segment target speed (distance > 80 mm) */
#define SLOW_RPM               10     /* slow-segment target speed (distance <= 80 mm) */

#define ACCEL_FAST             10     /* acceleration: +10 RPM per update */
#define ACCEL_BRAKE            20     /* deceleration: -20 RPM per update */

#define SPEED_UPDATE_INTERVAL_MS 100  /* speed update interval [ms] */

/* ========== Distance thresholds ========== */
#define DIST_THRESHOLD         80     /* speed switch [mm]: > 80 fast, <= 80 slow */
#define DIST_THRESHOLD_LOW     40     /* forced low speed [mm]: <= 40 -> 10 RPM */
#define DIST_THRESHOLD_STOP    30     /* return trigger [mm]: < 30 starts the return */

/* ========== Return parameters ========== */
#define RETURN_RPM             50     /* return speed [RPM] */

/* ========== Start-up timing ========== */
#define STARTUP_DELAY_MS       3000   /* start-up delay [ms] */
#define SAMPLE_INTERVAL_MS     100    /* sampling interval [ms] */

/* ========== Timer parameters ==========
 * TIM2 clock = 72 MHz (APB1 clock doubled)
 * Prescaler  = 72-1 -> 1 MHz timer clock (1 us resolution)
 * ARR        = computed from the target RPM
 */
#define TIM_PRESCALER          72-1   /* 72 MHz -> 1 MHz */

/* ========== Motor control API ========== */
void Stepper_Init(void);                    /* init the stepper pins and TIM2 */
void Stepper_Enable(void);                  /* enable the driver */
void Stepper_Disable(void);                 /* disable the driver */
void Stepper_SetDir(uint8_t dir);           /* direction: 0 = forward, 1 = reverse */
void Stepper_Start(void);                   /* start TIM2 */
void Stepper_Stop(void);                    /* stop TIM2 */
void Stepper_SetSpeed(uint16_t rpm);        /* set the target speed (updates ARR) */
void Stepper_UpdateSpeed(uint16_t target_rpm, uint16_t accel_per_update);  /* smooth ramp */
uint16_t Stepper_GetCurrentRPM(void);       /* current speed */

/* ========== Fixed-step moves (orange-press workflow) ==========
 * Pulses are counted inside the TIM2 update interrupt; once the target is
 * reached the timer stops and the driver keeps holding torque. The position
 * error is <= 1 step and does not depend on the main loop.
 */
void     Stepper_MoveSteps(uint32_t steps, uint16_t rpm, uint8_t dir);  /* start a move (auto-enable; ignored if steps/rpm are 0) */
void     Stepper_MoveStepsPps(uint32_t steps, uint32_t pps, uint8_t dir); /* move at a pulse rate (mm/s motion, not limited to integer RPM) */
uint8_t  Stepper_MoveBusy(void);            /* 1 = moving, 0 = finished / stopped */
void     Stepper_MoveAbort(void);           /* stop now (torque is kept; remaining steps stay readable) */
uint32_t Stepper_GetRemainingSteps(void);   /* steps left in the last move */

#endif
