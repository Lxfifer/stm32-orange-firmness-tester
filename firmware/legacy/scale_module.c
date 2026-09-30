/**
  * @file    scale_module.c
  * @brief   Digital load-cell transmitter module (custom TTL protocol) - HAL port
  *          on USART3 (PB10/PB11).
  *
  * Based on the ScaleModule.c of the standard-peripheral reference project; the
  * frame layout and the TX arrays are unchanged.
  *
  * RX bytes are pushed into a ring buffer by the USART3 receive interrupt. Every
  * command follows the same pattern: flush the buffer -> send the frame -> scan
  * for a valid reply.
  *
  * ARCHIVED: this is the alternative weighing front end (a transmitter module
  * instead of the HX711). It is not part of the current Keil build; the active
  * firmware uses hx711.c. See docs/07-history.md.
  */
#include "scale_module.h"
#include <string.h>

#define SM_RX_SIZE    64u     /* ring buffer size */
#define SM_MAX_FRAME  20u     /* largest reply frame (20 bytes for parameters) */
#define SM_LEN_10     10u
#define SM_LEN_20     20u

#define SM_FRAME_SYNC 0xAAu
#define SM_FRAME_END  0xFFu

static UART_HandleTypeDef husart3;

static volatile uint8_t  sm_rx[SM_RX_SIZE];
static volatile uint16_t sm_rx_w = 0;   /* write pointer (interrupt side) */
static volatile uint16_t sm_rx_r = 0;   /* read pointer (main side) */

/* ============ USART3 receive interrupt: byte into the ring ============ */
void USART3_IRQHandler(void)
{
    uint32_t sr = USART3->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE))
    {
        uint8_t d = (uint8_t)(USART3->DR & 0xFFu);
        if (sr & USART_SR_RXNE)
        {
            uint16_t nx = (uint16_t)((sm_rx_w + 1u) % SM_RX_SIZE);
            if (nx != sm_rx_r)          /* drop the byte when the ring is full */
            {
                sm_rx[sm_rx_w] = d;
                sm_rx_w = nx;
            }
        }
    }
}

/* ============ Initialise USART3 (PB10 = TX, PB11 = RX) ============ */
void SM_UartInit(uint32_t baud)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    /* PB10 -> USART3_TX */
    g.Pin   = GPIO_PIN_10;
    g.Mode  = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);

    /* PB11 <- USART3_RX */
    g.Pin   = GPIO_PIN_11;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);

    husart3.Instance        = USART3;
    husart3.Init.BaudRate   = baud;
    husart3.Init.WordLength = UART_WORDLENGTH_8B;
    husart3.Init.StopBits   = UART_STOPBITS_1;
    husart3.Init.Parity     = UART_PARITY_NONE;
    husart3.Init.Mode       = UART_MODE_TX_RX;
    husart3.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    husart3.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&husart3);

    /* Important: enable the RXNE interrupt source, otherwise USART3_IRQHandler
     * never fires and no byte ever reaches the ring buffer. */
    __HAL_UART_ENABLE_IT(&husart3, UART_IT_RXNE);

    HAL_NVIC_SetPriority(USART3_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

/* ============ Transmit ============ */
void SM_SendBytes(const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        while ((USART3->SR & USART_SR_TXE) == 0u) { }
        USART3->DR = buf[i];
    }
    /* Important: wait for TC (the whole frame has left the pin) before
     * returning. Waiting only for TXE would leave the last byte in the shift
     * register, and changing the baud rate at that moment would corrupt the
     * end of the frame. */
    while ((USART3->SR & USART_SR_TC) == 0u) { }
}

/* ============ Change the link baud rate (call after a complete frame) ============ */
void SM_SetBaud(uint32_t baud)
{
    __HAL_UART_DISABLE(&husart3);
    husart3.Init.BaudRate = baud;
    HAL_UART_Init(&husart3);
    __HAL_UART_ENABLE(&husart3);
    __HAL_UART_ENABLE_IT(&husart3, UART_IT_RXNE);  /* Init rewrites CR1, so RXNE must be re-enabled */
    SM_FlushRx();
}

/* ============ Send a raw FB parameter-write frame ============
 * Waits until the frame is out but does not wait for the reply; used when the
 * baud rate is about to change. */
void SM_SendParamsRaw(const SM_Param *p)
{
    uint8_t tx[15];
    uint8_t y = 0u;
    int i;

    tx[0]  = 0xFBu;
    tx[1]  = p->addr;
    tx[2]  = p->freq;
    tx[3]  = p->div;
    tx[4]  = p->baud;
    tx[5]  = p->zeroRange;
    tx[6]  = p->zeroEn;
    tx[7]  = 0x50u;
    tx[8]  = p->median;
    tx[9]  = p->avg;
    tx[10] = p->track;
    tx[11] = p->creep;
    tx[12] = p->stable;
    tx[13] = p->zeroStr;
    for (i = 0; i < 14; i++) y ^= tx[i];
    tx[14] = y;

    SM_FlushRx();
    SM_SendBytes(tx, 15);   /* waits for TC internally: the frame leaves the pin completely */
}

/* ============ Ring buffer ============ */
void SM_FlushRx(void)
{
    sm_rx_r = sm_rx_w;
}

static int sm_get(uint8_t *b)
{
    uint16_t r = sm_rx_r;
    if (r == sm_rx_w) return 0;
    *b = sm_rx[r];
    sm_rx_r = (uint16_t)((r + 1u) % SM_RX_SIZE);
    return 1;
}

/* A reply can arrive across several polls (about 10 ms per frame at 9600 baud),
 * so the partially received window has to be kept between calls. */
static uint8_t  sm_win[SM_MAX_FRAME];
static uint16_t sm_win_n = 0;

/* Validate one frame starting at the front of the buffer. */
static int sm_frame_ok(const uint8_t *f, uint8_t want1, uint8_t want2, uint16_t len)
{
    uint32_t sum = 0u;
    int k;

    if (f[0] != SM_FRAME_SYNC || f[len - 1] != SM_FRAME_END) return 0;
    if (f[1] != want1 && f[1] != want2) return 0;
    for (k = 1; k <= (int)len - 4; k++) sum += f[k];
    return (sum == (((uint32_t)f[len - 3] << 8) | f[len - 2]));
}

/* Poll for at most waitMs milliseconds waiting for a valid frame. */
static int sm_wait_frame(uint8_t want1, uint8_t want2, uint16_t len,
                         uint8_t *out, uint16_t waitMs)
{
    uint32_t end = HAL_GetTick() + waitMs;
    uint16_t st;
    int k;

    for (;;)
    {
        uint8_t b;
        while (sm_get(&b))
        {
            if (sm_win_n < SM_MAX_FRAME)
            {
                sm_win[sm_win_n++] = b;
            }
            else    /* window full of junk: keep only the newest byte */
            {
                for (k = 1; k < (int)SM_MAX_FRAME; k++) sm_win[k - 1] = sm_win[k];
                sm_win[SM_MAX_FRAME - 1] = b;
            }
            if (sm_win_n >= len)
            {
                for (st = 0; st + len <= sm_win_n; st++)
                {
                    if (sm_frame_ok(&sm_win[st], want1, want2, len))
                    {
                        for (k = 0; k < (int)len; k++) out[k] = sm_win[st + (uint16_t)k];
                        for (k = (int)(st + len); k < (int)sm_win_n; k++)
                            sm_win[k - (int)(st + len)] = sm_win[k];
                        sm_win_n -= (uint16_t)(st + len);
                        return 1;
                    }
                }
            }
        }
        if (HAL_GetTick() >= end) break;
    }
    return 0;
}

/* Decode the weight (10-byte reply, sign in bit 0 of byte 3). */
static void sm_decode_weight(const uint8_t *rx, int32_t *weight)
{
    uint32_t mag = ((uint32_t)rx[4] << 16) | ((uint32_t)rx[5] << 8) | rx[6];
    *weight = (rx[3] & 0x01u) ? -(int32_t)mag : (int32_t)mag;
}

/* ============ Module commands ============ */
int SM_ReadParams(SM_Param *p)
{
    static const uint8_t tx[5] = { 0xF2u, 0x00u, 0xF1u, 0xF3u, 0xF0u };
    uint8_t rx[SM_LEN_20];

    SM_FlushRx();
    SM_SendBytes(tx, 5);
    if (!sm_wait_frame(0xF2u, 0xF2u, SM_LEN_20, rx, 800u)) return 0;

    p->addr      = rx[2];
    p->freq      = rx[3];
    p->div       = rx[4];
    p->baud      = rx[5];
    p->zeroRange = rx[6];
    p->zeroEn    = rx[7];
    p->median    = rx[9];
    p->avg       = rx[10];
    p->track     = rx[11];
    p->creep     = rx[12];
    p->stable    = rx[13];
    p->zeroStr   = rx[14];
    return 1;
}

int SM_WriteParams(const SM_Param *p)
{
    uint8_t tx[15];
    uint8_t y = 0u;
    int i;

    tx[0]  = 0xFBu;
    tx[1]  = p->addr;
    tx[2]  = p->freq;
    tx[3]  = p->div;
    tx[4]  = p->baud;
    tx[5]  = p->zeroRange;
    tx[6]  = p->zeroEn;
    tx[7]  = 0x50u;      /* fixed code */
    tx[8]  = p->median;
    tx[9]  = p->avg;
    tx[10] = p->track;
    tx[11] = p->creep;
    tx[12] = p->stable;
    tx[13] = p->zeroStr;
    for (i = 0; i < 14; i++) y ^= tx[i];
    tx[14] = y;

    SM_FlushRx();
    SM_SendBytes(tx, 15);
    {
        uint8_t rx[SM_LEN_20];
        return sm_wait_frame(0xFBu, 0xF2u, SM_LEN_20, rx, 800u);
    }
}

int SM_Tare(int32_t *net)
{
    static const uint8_t tx[5] = { 0xABu, 0x00u, 0xAAu, 0xACu, 0xADu };
    uint8_t rx[SM_LEN_10];

    SM_FlushRx();
    SM_SendBytes(tx, 5);
    if (!sm_wait_frame(0xABu, 0xABu, SM_LEN_10, rx, 1500u)) return 0;
    sm_decode_weight(rx, net);
    return 1;
}

int SM_StartRead(void)
{
    static const uint8_t tx[5] = { 0xA3u, 0x00u, 0xA2u, 0xA4u, 0xA5u };
    SM_FlushRx();
    SM_SendBytes(tx, 5);
    return 1;
}

int SM_PollWeight(int32_t *w, uint16_t waitMs)
{
    uint8_t rx[SM_LEN_10];
    if (!sm_wait_frame(0xA3u, 0xA3u, SM_LEN_10, rx, waitMs)) return 0;
    sm_decode_weight(rx, w);
    return 1;
}
