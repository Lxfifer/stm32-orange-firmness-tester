/*****************************************************************************
  Project : Orange-press tester - merged firmware (automatic mode "a" +
            manual mode "b" in a single image, two workflows)
  Target  : STM32F103C8T6, Keil MDK-ARM (AC5), STM32Cube HAL (FW_F1 V1.8.4)

  Console (USART1, 115200 8N1, plain ASCII, commands are case-insensitive):
    a = AUTOMATIC mode (former "main program J")
        tare -> lift 110 mm (skipped when the rod is already up)
        -> wait for the orange (weight > 20 g for 2 s)
        -> sample A over 0.5 s -> cut the motor so the rod falls freely
        -> contact detected (weight > A + 20 g) -> wait for a stable 1 s
        -> re-tare (this is the pre-press zero) -> press down press_mm
           (default 3 mm, change with "c=<mm>")
        -> record MAX over 1 s -> silently retract 40 mm
    b = MANUAL mode (former "main program K")
        tare (motor de-energised) -> operator places the rod on the orange
        -> contact detected (> 20 g for 2 s) -> sample A over 0.5 s
        -> re-tare -> press down press_mm (default 3 mm)
        -> record MAX over 1 s -> motor off (rod can be moved by hand)
    d = cut the motor and return to idle at any time
    t = idle self-test: up 20 mm then down 20 mm
        (verifies the motor and the wiring; requires idle + rod at the bottom)
    v = diagnostics: prints state/rod_up/motor_busy/scale_ok/press
        (use it to find out where a run got stuck)
    c = <mm> followed by CR/LF sets the press depth, e.g. "c=2.5"
        (accepted in idle or after a finished run; clamped to 0.1..20 mm)

  Weighing : HX711 (PC13 = DT, PC14 = SCK), ~10 Hz
  Scaling  : 80.386 steps/mm (measured); 30 mm/s = 2412 pps, 1 mm/s = 80 pps
*****************************************************************************/
#include "main.h"
#include "gpio.h"
#include "usart.h"
#include "hx711.h"
#include <stdio.h>
#include <string.h>
#include "stepper.h"

void SystemClock_Config(void);

/* Retarget printf to USART1 */
int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/* ===== USART1 RX: single-byte interrupt latch ===== */
static volatile uint8_t u1_rx_byte = 0;
static volatile uint8_t u1_rx_flag = 0;
void USART1_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE) != RESET)
    {
        u1_rx_byte = (uint8_t)(huart1.Instance->DR & 0xFF);
        u1_rx_flag = 1;
    }
    /* Reading DR clears the overrun flag; the byte itself is discarded. */
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE) != RESET)
    {
        (void)(huart1.Instance->DR & 0xFF);
    }
}

/* ================= Run states ================= */
#define ST_IDLE         0
#define ST_J_LIFT       1   /* J: lifting 110 mm after tare */
#define ST_J_WAIT       2   /* J: waiting for the orange to be placed */
#define ST_J_A          3   /* J: 0.5 s averaging of the "free" weight */
#define ST_J_FALL       4   /* J: free fall until contact */
#define ST_J_STABLE     5   /* J: waiting for a stable reading after contact */
#define ST_J_PRESS      6   /* J: pressing down press_mm */
#define ST_J_MAX        7   /* J: 1 s window capturing the peak force */
#define ST_J_RISE       8   /* J: silent 40 mm retract */
#define ST_J_DONE       9   /* J: finished, rod held in place */
#define ST_K_WAIT       10  /* K: waiting for the hand-placed rod */
#define ST_K_A          11  /* K: 0.5 s averaging at contact */
#define ST_K_PRESS      12  /* K: pressing down press_mm */
#define ST_K_MAX        13  /* K: 1 s window capturing the peak force */
#define ST_K_DONE       14  /* K: finished, motor released */

/* ================= Unit conversion (measured: 80.386 steps/mm) ================= */
#define STEPS_OF(MM)   ((uint32_t)((MM) * 80.386f + 0.5f))
#define PPS_OF(MMS)    ((uint32_t)((MMS) * 80.386f + 0.5f))
#define STEPS_110MM    STEPS_OF(110.0f)   /* initial lift of the automatic mode */
#define STEPS_40MM     STEPS_OF(40.0f)    /* silent retract after MAX */
#define STEPS_1MM      STEPS_OF(1.0f)
#define PPS_30         PPS_OF(30.0f)      /* fast travel: 30 mm/s */
#define PPS_1          PPS_OF(1.0f)       /* pressing:   1 mm/s */
#define DIR_UP         0
#define DIR_DOWN       1

/* ================= Detection / timing ================= */
#define DETECT_G       20.0f    /* object-present threshold [g] */
#define DETECT_MS      2000     /* ... held for this long [ms] */
#define A_WIN_MS       500      /* averaging window of the free weight A [ms] */
#define CONTACT_G      20.0f    /* contact = weight above A by this much [g] */
#define FALL_IGNORE_MS 150      /* ignore the first instants of the fall [ms] */
#define STABLE_WIN_MS  1000     /* stability window [ms] */
#define STABLE_AMP_G   2.0f     /* ... max peak-to-peak amplitude [g] */
#define MAX_WIN_MS     1000     /* peak-capture window after pressing [ms] */
#define FALL_WARN_MS   10000    /* warn if no contact after this long [ms] */
#define PRINT_MS       100      /* 10 Hz streaming (HX711 runs at ~10 SPS) */

/* ================= Variables ================= */
static uint8_t  state = ST_IDLE;
static uint32_t entry_tick = 0;         /* tick at which the current state was entered */
static uint32_t last_print = 0;         /* last "W <g>" streaming timestamp */
static uint8_t  over20_active = 0;      /* object-present detection in progress */
static uint32_t over20_tick = 0;        /* timestamp of the first sample above 20 g */
static uint8_t  warn_printed = 0;       /* one-shot flag for the "no contact" error */
static uint8_t  quiet_done = 0;         /* suppress 10 Hz streaming after MAX */
static uint8_t  rod_up = 0;             /* rod is at the top (motor still holding) */

static float a_val = 0.0f, win_sum = 0.0f, max_val = 0.0f;

static float press_mm = 3.0f;       /* press depth [mm], default 3; set with c=<mm> */
static char  rx_line[24];           /* line-command buffer */
static uint8_t rx_n = 0;
static uint16_t win_cnt = 0;
static uint8_t  scale_fail_cnt = 0;   /* consecutive failed HX711 reads */
static uint8_t  scale_warned = 0;     /* this failure has already been reported */


/* Ring buffer of recent (weight, timestamp) pairs, used by the stability test. */
#define RING_N  128
static float    ring_w[RING_N];
static uint32_t ring_t[RING_N];
static uint8_t  ring_i = 0, ring_n = 0;
static void Ring_Clear(void) { ring_n = 0; ring_i = 0; }
static void Ring_Push(uint32_t now, float w)
{
    ring_w[ring_i] = w; ring_t[ring_i] = now;
    ring_i = (ring_i + 1) % RING_N;
    if (ring_n < RING_N) ring_n++;
}
/* 1 = the samples inside the last STABLE_WIN_MS span at most STABLE_AMP_G */
static int StableOK(uint32_t now)
{
    uint8_t i, n = ring_n, cnt = 0;
    float mn = 1e9f, mx = -1e9f;
    for (i = 0; i < n; i++)
    {
        uint8_t idx = (ring_i + RING_N - 1 - i) % RING_N;
        if ((uint32_t)(now - ring_t[idx]) <= STABLE_WIN_MS)
        {
            if (ring_w[idx] < mn) mn = ring_w[idx];
            if (ring_w[idx] > mx) mx = ring_w[idx];
            cnt++;
        }
    }
    if (cnt < 2) return 0;
    return ((mx - mn) <= STABLE_AMP_G) ? 1 : 0;
}

/* ================= Printing ================= */
/* Print a weight with 2 decimals. Integer arithmetic keeps %f out of the
 * binary, which matches MicroLIB and the default printf settings. */
static void PrintW(float v)
{
    int32_t cent = (v >= 0) ? (int32_t)(v * 100.0f + 0.5f) : (int32_t)(v * 100.0f - 0.5f);
    int32_t wh = cent / 100, fr = cent % 100;
    if (fr < 0) fr = -fr;
    if (fr < 10) printf("%ld.0%ld", (long)wh, (long)fr);
    else         printf("%ld.%ld", (long)wh, (long)fr);
}

/* ================= Weighing (HX711: PC13 = DT, PC14 = SCK, ~10 Hz) ================= */
static void ScaleTare(void)
{
    HX711_GetTare();          /* no-load tare (zero point kept inside the driver) */
}

static float ScaleGetG(void)
{
    return HX711_GetWeight(); /* one reading; blocks until the next conversion (~10 Hz) */
}
/* ================= Actions ================= */
static void MotorOff(void) { Stepper_MoveAbort(); Stepper_Disable(); }

static void ToIdle(uint32_t now)
{
    MotorOff();
    state = ST_IDLE; entry_tick = now;
    quiet_done = 0; warn_printed = 0; rod_up = 0;
    printf("ready: a=auto b=manual d=off c=<mm> (press def 3mm)\r\n");
}

/* Tare and report the result instead of failing silently. */
static void DoTareNote(void)
{
    ScaleTare();
    /* A tare only counts as done when enough valid samples were collected;
     * if the sensor does not answer, say so instead of waiting forever. */
    if (!HX711_TareHadEnough())
    {
        printf("err scale no reply - check HX711 (power/VBAT/PC13-PC14 wiring)\r\n");
    }
    else
    {
        printf("zero\r\n");   /* tare finished */
    }
}

/* J: start the automatic run (tare; lift 110 mm unless the rod is already up) */
static void StartJ(uint32_t now)
{
    DoTareNote();
    quiet_done = 0; warn_printed = 0; over20_active = 0; Ring_Clear();
    if (!rod_up)
    {
        printf("lift 110\r\n");
        Stepper_MoveStepsPps(STEPS_110MM, PPS_30, DIR_UP);
        state = ST_J_LIFT; entry_tick = now;
    }
    else
    {
        state = ST_J_WAIT; entry_tick = now;
        printf("place orange\r\n");
    }
}

/* K: start the manual run (tare, motor released, operator moves the rod) */
static void StartK(uint32_t now)
{
    MotorOff();
    rod_up = 0;
    DoTareNote();
    quiet_done = 0; over20_active = 0;
    state = ST_K_WAIT; entry_tick = now;
    printf("place rod on orange\r\n");
}

/* ================= State machine ================= */
static void RunState(uint32_t now, float w)
{
    uint32_t el = now - entry_tick;    /* time spent in the current state */

    switch (state)
    {
    case ST_IDLE:
        break;

    /* J: lifting 110 mm */
    case ST_J_LIFT:
        if (!Stepper_MoveBusy())
        {
            rod_up = 1;
            state = ST_J_WAIT; entry_tick = now;
            printf("place orange\r\n");
        }
        break;

    /* Waiting for the orange (shared by J and K): > 20 g for 2 s */
    case ST_J_WAIT:
    case ST_K_WAIT:
        if (w >= DETECT_G)
        {
            if (!over20_active) { over20_active = 1; over20_tick = now; }
            else if (now - over20_tick >= DETECT_MS)
            {
                win_sum = 0; win_cnt = 0;
                state = (state == ST_J_WAIT) ? ST_J_A : ST_K_A;
                entry_tick = now;
                
            }
        }
        else if (over20_active)
        {
            over20_active = 0;
            
        }
        break;

    /* A = 0.5 s average of the weight measured before the press */
    case ST_J_A:
        if (el >= A_WIN_MS)
        {
            a_val = (win_cnt > 0) ? win_sum / (float)win_cnt : w;
            printf("A ");
            PrintW(a_val);
            printf(" g (%u)\r\n", (unsigned)win_cnt);
            MotorOff();
            Ring_Clear();
            state = ST_J_FALL; entry_tick = now; warn_printed = 0;
            
        }
        else { win_sum += w; win_cnt++; }
        break;

    /* J: free fall until the rod touches the orange */
    case ST_J_FALL:
        if (el >= FALL_IGNORE_MS && w > a_val + CONTACT_G)
        {
            Ring_Clear();
            Ring_Push(now, w);
            state = ST_J_STABLE; entry_tick = now; warn_printed = 0;
            printf("contact\r\n");
        }
        else if (el >= FALL_WARN_MS && !warn_printed)
        {
            warn_printed = 1;
            printf("err no contact (10s) - check orange\r\n");
        }
        break;

    /* Wait until the contact reading is stable, then re-tare and press. */
    case ST_J_STABLE:
        Ring_Push(now, w);
        if (StableOK(now))
        {
            ScaleTare();   /* tare in the contact state = pre-press zero */
            /* press down press_mm at 1 mm/s */
            Stepper_MoveStepsPps((uint32_t)(press_mm * 80.386f + 0.5f), PPS_1, DIR_DOWN);
            state = ST_J_PRESS; entry_tick = now;
            printf("press ");
            PrintW(press_mm);
            printf(" mm\r\n");
        }
        break;

    case ST_J_PRESS:
        if (!Stepper_MoveBusy())
        {
            printf("press done\r\n");
            max_val = 0.0f;
            state = ST_J_MAX; entry_tick = now;
            
        }
        break;

    /* Capture the peak force for MAX_WIN_MS, then retract silently. */
    case ST_J_MAX:
        if (w > max_val) max_val = w;
        if (el >= MAX_WIN_MS)
        {
            printf("sample done\r\n");
            printf("MAX ");
            PrintW(max_val);
            printf(" g\r\n");
            quiet_done = 1;
            Stepper_MoveStepsPps(STEPS_40MM, PPS_30, DIR_UP);  /* silent 40 mm retract */
            state = ST_J_RISE; entry_tick = now;
        }
        break;

    case ST_J_RISE:
        if (!Stepper_MoveBusy())
        {
            printf("done\r\n");
            state = ST_J_DONE; entry_tick = now;   /* rod stays up, motor holding */
        }
        break;

    case ST_J_DONE:
        break;

    /* K: average at contact -> re-tare -> press press_mm */
    case ST_K_A:
        if (el >= A_WIN_MS)
        {
            a_val = (win_cnt > 0) ? win_sum / (float)win_cnt : w;
            printf("A ");
            PrintW(a_val);
            printf(" g (%u)\r\n", (unsigned)win_cnt);
            ScaleTare();
            
            Stepper_MoveStepsPps((uint32_t)(press_mm * 80.386f + 0.5f), PPS_1, DIR_DOWN);
            state = ST_K_PRESS; entry_tick = now;
            printf("press ");
            PrintW(press_mm);
            printf(" mm\r\n");
        }
        else { win_sum += w; win_cnt++; }
        break;

    case ST_K_PRESS:
        if (!Stepper_MoveBusy())
        {
            printf("press done\r\n");
            max_val = 0.0f;
            state = ST_K_MAX; entry_tick = now;
            
        }
        break;

    case ST_K_MAX:
        if (w > max_val) max_val = w;
        if (el >= MAX_WIN_MS)
        {
            printf("sample done\r\n");
            printf("MAX ");
            PrintW(max_val);
            printf(" g\r\n");
            printf("done\r\n");
            quiet_done = 1;
            MotorOff();
            state = ST_K_DONE; entry_tick = now;   /* rod can be moved by hand */
        }
        break;

    case ST_K_DONE:
        break;
    }
}

/* ================= Single-character commands ================= */
static void HandleCmd(uint8_t ch)
{
    uint32_t now = HAL_GetTick();
    /* A new run may only be started from idle or after a finished run. */
    uint8_t canRun = (state == ST_IDLE || state == ST_J_DONE || state == ST_K_DONE);
    /* States in which a second start command must be ignored. */
    uint8_t busy = (state == ST_J_WAIT || state == ST_J_A || state == ST_J_FALL ||
                    state == ST_J_STABLE || state == ST_J_PRESS || state == ST_J_MAX ||
                    state == ST_J_RISE || state == ST_J_LIFT || state == ST_K_WAIT ||
                    state == ST_K_A || state == ST_K_PRESS || state == ST_K_MAX);

    /* Echo a/b/d back in lower case, as documented in the communication protocol. */
    if (ch == 'A' || ch == 'B' || ch == 'D')
    {
        printf("%c\r\n", ch + 32);
    }

    if (ch == 'A')                       /* automatic mode (former "j") */
    {
        if (busy) return;
        if (canRun)
        {
            StartJ(now);
        }
    }
    else if (ch == 'B')                  /* manual mode (former "k") */
    {
        if (busy) return;
        if (canRun)
        {
            StartK(now);
        }
    }
    else if (ch == 'D')
    {
        if (state != ST_IDLE) ToIdle(now);
    }
    else if (ch == 'V')
    {
        /* Diagnostics: dump the current state and the key flags, so a stuck
         * run can be traced back to a specific step. */
        const char *sn = "?";
        switch (state)
        {
            case ST_IDLE:      sn = "IDLE"; break;
            case ST_J_LIFT:    sn = "J_LIFT"; break;
            case ST_J_WAIT:    sn = "J_WAIT"; break;
            case ST_J_A:       sn = "J_A"; break;
            case ST_J_FALL:    sn = "J_FALL"; break;
            case ST_J_STABLE:  sn = "J_STABLE"; break;
            case ST_J_PRESS:   sn = "J_PRESS"; break;
            case ST_J_MAX:     sn = "J_MAX"; break;
            case ST_J_RISE:    sn = "J_RISE"; break;
            case ST_J_DONE:    sn = "J_DONE"; break;
            case ST_K_WAIT:    sn = "K_WAIT"; break;
            case ST_K_A:       sn = "K_A"; break;
            case ST_K_PRESS:   sn = "K_PRESS"; break;
            case ST_K_MAX:     sn = "K_MAX"; break;
            case ST_K_DONE:    sn = "K_DONE"; break;
        }
        printf("state=%s rod_up=%d motor_busy=%d scale_ok=%d press=",
               sn, (int)rod_up, (int)Stepper_MoveBusy(), (int)HX711_LastOk());
        PrintW(press_mm);
        printf(" mm\r\n");
    }
    else if (ch == 'T')
    {
        /* Idle self-test: up 20 mm then back down (verifies motor + direction). */
        if (state == ST_IDLE && !rod_up)
        {
            printf("t\r\n");
            Stepper_MoveStepsPps(STEPS_OF(20.0f), PPS_30, DIR_UP);
            while (Stepper_MoveBusy()) HAL_Delay(5);
            printf("t up20\r\n");
            HAL_Delay(200);
            Stepper_MoveStepsPps(STEPS_OF(20.0f), PPS_30, DIR_DOWN);
            while (Stepper_MoveBusy()) HAL_Delay(5);
            printf("t ok\r\n");
        }
        else
        {
            printf("t not allowed (need idle, rod low)\r\n");
        }
    }
    else if (ch >= 'A' && ch <= 'Z')
    {
        printf("? a=auto b=manual d=off c=<mm>\r\n");
    }
}


/* Line-command parser: C=<mm> sets the press depth (for example "c=2.5"). */
static void HandleLine(void)
{
    int j = 2, fdig = 0;
    int seen_dot = 0;
    long iv = 0, fnum = 0, cent;

    rx_line[rx_n] = 0;

    if (rx_line[0] == 'C' && rx_line[1] == '=')
    {
        /* Parameters are only accepted while idle or after a finished run;
         * during a run the line is ignored. */
        if (!(state == ST_IDLE || state == ST_J_DONE || state == ST_K_DONE))
        {
            printf("param ignored - set in idle only\r\n");
            rx_n = 0;
            return;
        }
        for (j = 2; rx_line[j] != 0 && j < (int)sizeof(rx_line); j++)
        {
            char q = rx_line[j];
            if (q == '.') { seen_dot = 1; }
            else if (q >= '0' && q <= '9')
            {
                /* at most two digits after the decimal point */
                if (!seen_dot) iv = iv * 10 + (q - '0');
                else if (fdig < 2) { fnum = fnum * 10 + (q - '0'); fdig++; }
            }
            else break;
        }
        cent = iv * 100 + (seen_dot ? ((fdig == 1) ? fnum * 10 : fnum) : 0);
        press_mm = (float)cent / 100.0f;
        if (press_mm < 0.1f) press_mm = 0.1f;
        if (press_mm > 20.0f) press_mm = 20.0f;
        printf("param ");
        PrintW(press_mm);
        printf(" mm\r\n");
    }
    else
    {
        printf("? a=auto b=manual d=off c=<mm>\r\n");
    }
    rx_n = 0;
}

/* One received byte: a/b/d/t/v act immediately, anything else is buffered and
 * interpreted as a line when CR or LF arrives. */
static void ProcessChar(uint8_t c)
{
    if (c >= 'a' && c <= 'z') c -= 32;

    if (c == '\r' || c == '\n')
    {
        if (rx_n > 0) HandleLine();
        return;
    }
    /* Single-character command, only when it is the first byte of a line. */
    if (rx_n == 0 && (c == 'A' || c == 'B' || c == 'D' || c == 'T' || c == 'V'))
    {
        HandleCmd(c);
        return;
    }
    if (rx_n < (int)sizeof(rx_line) - 1)
    {
        rx_line[rx_n++] = (char)c;
    }
    else
    {
        HandleLine();              /* buffer full: interpret what we have */
        rx_line[0] = (char)c;
        rx_n = 1;
    }
}
/* ================= Main ================= */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);

    printf("\r\n====== Orange-press merged: a=auto b=manual ======\r\n");
    printf("commands: a=auto b=manual d=off c=<mm>\r\n");

    Stepper_Init();
    HX711_GPIO_Init();
    HX711_GetTare();
    if (!HX711_TareHadEnough())
    {
        printf("err scale no reply - check HX711 (power/VBAT/PC13-PC14 wiring)\r\n");
    }
    printf("Current weight: ");
    PrintW(HX711_GetWeight());
    printf(" g\r\n\r\n");

    ToIdle(HAL_GetTick());

    while (1)
    {
        uint32_t now = HAL_GetTick();
        float w = ScaleGetG();

        /* Weighing health monitor: report a dead sensor once per failure burst,
         * but never abort a run because of it. */
        if (HX711_LastOk())
        {
            scale_fail_cnt = 0;
            scale_warned = 0;
        }
        else if (++scale_fail_cnt >= 8 && !scale_warned)
        {
            scale_warned = 1;
            printf("err scale no reply - check HX711 (power/VBAT/PC13-PC14 wiring)\r\n");
        }

        /* Input: interrupt latch plus a direct poll in the main loop
         * (belt and braces, so no byte is lost while a move is running). */
        while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE))
        {
            uint8_t c = (uint8_t)(huart1.Instance->DR & 0xFF);
            if (c >= 'a' && c <= 'z') c -= 32;
            ProcessChar(c);
        }
        if (u1_rx_flag)
        {
            u1_rx_flag = 0;
            uint8_t c = u1_rx_byte;
            if (c >= 'a' && c <= 'z') c -= 32;
            ProcessChar(c);
        }

        RunState(now, w);

        /* 10 Hz streaming while the orange is placed, while falling, while
         * stabilising and while pressing. After "press done" the link goes
         * quiet: MAX is still computed from the internal 1 s window, but the
         * samples are no longer sent out (the reading relaxes during the
         * capture window, so streaming them would only confuse the plot). */
        if (!quiet_done &&
            (state == ST_J_WAIT || state == ST_J_FALL || state == ST_J_STABLE ||
             state == ST_J_PRESS ||
             state == ST_K_WAIT || state == ST_K_PRESS) &&
            now - last_print >= PRINT_MS)
        {
            last_print = now;
            printf("W ");
            PrintW(w);
            printf(" g\r\n");
        }
    }
}

/* 72 MHz from the 8 MHz HSE crystal through the PLL (x9). */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

void Error_Handler(void) { }

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) { }
#endif
