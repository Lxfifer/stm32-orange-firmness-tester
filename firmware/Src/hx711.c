/**
  * @file    hx711.c
  * @brief   HX711 24-bit load-cell ADC driver (STM32 HAL version)
  *
  * Ported from the vendor-style load-cell project (HARDWARE/HX711, StdPeriph):
  *   1. GPIO access now uses the HAL.
  *   2. delay_us() is implemented with the DWT cycle counter (exact at 72 MHz).
  *   3. The original calibration formula is kept:
  *        weight = (raw - tare) * 0.01 * 0.00001 * XISHU
  *   4. Waiting for DOUT has a timeout, so a missing sensor cannot hang the app.
  *   5. One reading per call (~10 Hz at 10 SPS), smoothed with a sliding
  *      median filter.
  *
  * Wiring : SCK -> PC14, DT -> PC13
  * Note   : PC13/PC14 belong to the backup domain of the STM32F103. VBAT must
  *          be tied to 3.3 V (or to a battery); otherwise these pins do not
  *          work at all and every reading stays 0.
  */
#include "hx711.h"

/* Calibration factor, identical to the original scale project (4268).
 * It has to be re-calibrated for a different sensor, see docs/01-hardware.md:
 * load a known weight, note the displayed value X, then xishu = old * 1000 / X.
 */
#define HX711_XISHU   4268

/* Timeout while waiting for DOUT [ms]. One conversion takes about 100 ms at
 * 10 SPS, so this is generous. If the sensor is disconnected or unpowered,
 * DOUT stays high; without the timeout the whole firmware would block here. */
#define HX711_READ_TIMEOUT_MS   120

/* Sliding median filter depth. */
#define HX711_FILTER_LEN        5

static uint32_t tare_raw = 0;             /* raw value of the tare (zero) point */
static uint32_t filter_buf[HX711_FILTER_LEN];  /* sliding window */
static uint8_t  filter_count = 0;         /* number of valid samples in the window */
static uint8_t  filter_index = 0;         /* next write position */
static uint8_t  s_last_ok = 0;            /* last raw read succeeded */
static uint8_t  s_tare_ok = 0;            /* last tare collected enough samples */

/* ========== Microsecond delay via DWT (72 MHz) ========== */
static void HX711_DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * 72u;      /* 72 MHz: 1 us = 72 cycles */
    while ((DWT->CYCCNT - start) < cycles) { }
}

/* ========== Switch the DT pin between output and input ========== */
static void HX711_DT_Out(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = HX711_DT_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(HX711_DT_PORT, &GPIO_InitStruct);
}

static void HX711_DT_In(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = HX711_DT_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(HX711_DT_PORT, &GPIO_InitStruct);
}

/* ========== Initialisation ========== */
void HX711_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* SCK: push-pull output */
    GPIO_InitStruct.Pin   = HX711_SCK_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(HX711_SCK_PORT, &GPIO_InitStruct);

    /* DT: floating input */
    GPIO_InitStruct.Pin   = HX711_DT_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(HX711_DT_PORT, &GPIO_InitStruct);

    /* Enable the DWT cycle counter (microsecond delay and timeout checks). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* ========== Read one 24-bit raw sample ==========
 * Datasheet: while DOUT is high the conversion is not ready and PD_SCK must be
 * low. 24 clock pulses shift out the 24 data bits; a 25th pulse selects input
 * channel A with gain 128 for the next conversion.
 * @retval >=0 : valid reading
 * @retval -1  : timed out waiting for DOUT (sensor missing or not ready)
 */
static int32_t HX711_ReadRaw(void)
{
    uint8_t i;
    uint32_t value = 0;
    uint32_t start;

    /* Initial state: DT driven high, SCK low. */
    HX711_DT_Out();
    HAL_GPIO_WritePin(HX711_DT_PORT, HX711_DT_PIN, GPIO_PIN_SET);
    HX711_DelayUs(1);
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET);
    HX711_DT_In();

    /* Wait for DOUT to go low (conversion finished), with a timeout so a dead
     * sensor does not block the firmware. */
    start = DWT->CYCCNT;
    while (HAL_GPIO_ReadPin(HX711_DT_PORT, HX711_DT_PIN) == GPIO_PIN_SET)
    {
        if ((DWT->CYCCNT - start) >= (uint32_t)(HX711_READ_TIMEOUT_MS * 72000UL))
        {
            s_last_ok = 0;
            return -1;   /* Timed out: return without clocking, so the chip state
                            is left untouched and the next call can retry. */
        }
    }
    HX711_DelayUs(1);

    /* Shift out the 24 data bits, MSB first. */
    for (i = 0; i < 24; i++)
    {
        HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_SET);
        HX711_DelayUs(1);
        value <<= 1;
        if (HAL_GPIO_ReadPin(HX711_DT_PORT, HX711_DT_PIN) == GPIO_PIN_SET)
        {
            value |= 0x01;
        }
        HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET);
        HX711_DelayUs(1);
    }

    /* 25th pulse: select channel A, gain 128. */
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_SET);
    HX711_DelayUs(1);
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET);
    HX711_DelayUs(1);

    value ^= 0x800000;   /* two's complement of the 24-bit value */
    s_last_ok = 1;
    return (int32_t)value;
}

/* Public API: read one raw sample, return 0 on timeout (kept compatible with
 * the older API). */
uint32_t Read_HX711(void)
{
    int32_t raw = HX711_ReadRaw();
    return (raw >= 0) ? (uint32_t)raw : 0u;
}

/* ========== Sliding median filter ========== */
static void Filter_Push(uint32_t v)
{
    filter_buf[filter_index] = v;
    filter_index = (filter_index + 1) % HX711_FILTER_LEN;
    if (filter_count < HX711_FILTER_LEN)
    {
        filter_count++;
    }
}

static uint32_t Filter_Median(void)
{
    uint32_t tmp[HX711_FILTER_LEN];
    uint8_t i, j, n = filter_count;

    if (n == 0) return 0;

    for (i = 0; i < n; i++) tmp[i] = filter_buf[i];

    /* Simple bubble sort; the window is only five samples deep. */
    for (i = 0; i + 1 < n; i++)
    {
        for (j = i + 1; j < n; j++)
        {
            if (tmp[i] > tmp[j])
            {
                uint32_t t = tmp[i];
                tmp[i] = tmp[j];
                tmp[j] = t;
            }
        }
    }
    return tmp[n / 2];   /* median */
}

/* ========== No-load tare ==========
 * Fill the window with valid samples and take the median; at most 10 attempts
 * when the sensor does not answer. */
void HX711_GetTare(void)
{
    uint8_t i;

    filter_count = 0;
    filter_index = 0;
    s_tare_ok    = 0;

    for (i = 0; i < 10; i++)
    {
        int32_t raw = HX711_ReadRaw();
        if (raw >= 0)
        {
            Filter_Push((uint32_t)raw);
        }
        if (filter_count >= HX711_FILTER_LEN)
        {
            break;
        }
    }

    /* Remember whether the tare really saw enough samples (i.e. the scale is
     * alive), so the main program can warn instead of silently misbehaving. */
    if (filter_count >= HX711_FILTER_LEN) s_tare_ok = 1;

    tare_raw = Filter_Median();

    /* Clear the window so normal operation starts collecting fresh samples. */
    filter_count = 0;
    filter_index = 0;
}

/* ========== Weight in grams ==========
 * One reading per call (~10 Hz at 10 SPS), pushed into the sliding window and
 * reduced to its median, then tare-corrected. Same formula as the original
 * load-cell project:
 *   weight = (median - tare) * 0.01 * 0.00001 * calibration factor
 */
float HX711_GetWeight(void)
{
    int32_t raw = HX711_ReadRaw();
    float w;

    if (raw >= 0)
    {
        Filter_Push((uint32_t)raw);
    }
    /* On timeout the old samples are kept, so the reading holds its last value. */

    w = ((float)Filter_Median() - (float)tare_raw) * 0.01f * 0.00001f * (float)HX711_XISHU;
    if (w < 0.0f)
    {
        w = 0.0f;
    }
    return w;
}

/* 1 = the last raw read succeeded (used by the main program to report faults). */
uint8_t HX711_LastOk(void)
{
    return s_last_ok;
}

/* 1 = the last tare collected enough valid samples (scale is responding). */
uint8_t HX711_TareHadEnough(void)
{
    return s_tare_ok;
}
