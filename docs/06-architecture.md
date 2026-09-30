# 06 - Firmware architecture

## 1. Module map

| File | Responsibility |
|---|---|
| `Src/main.c` | application: console, command handling, both run workflows, stability ring buffer, HX711 health monitor, clock configuration |
| `Src/hx711.c`, `Inc/hx711.h` | HX711 driver: bit-banged 24-bit read, DWT microsecond delay, sliding median filter, tare, error flags |
| `Src/stepper.c`, `Inc/stepper.h` | HPD322 control: TIM2 pulse generation, fixed-step moves, smooth RPM ramping |
| `Src/usart.c`, `Inc/usart.h` | USART1 handle and initialisation (CubeMX generated) |
| `Src/gpio.c`, `Inc/gpio.h` | GPIO clock enable (CubeMX generated) |
| `Src/stm32f1xx_hal_msp.c` | MSP init: AFIO/PWR clocks, SWD remap, USART1 pins |
| `Src/stm32f1xx_it.c` | Cortex-M core exception handlers (CubeMX generated) |
| `Src/system_stm32f1xx.c` | CMSIS system file (CubeMX generated) |
| `Src/sys.c`, `Inc/sys.h` | CubeMX generator for the SWD remap; **not** compiled |
| `legacy/scale_module.c/.h` | alternative weighing front end (transmitter module); **not** compiled |
| `legacy/oled.c/.h`, `oledfont.h` | 128×64 OLED reference driver; **not** compiled |

Three interrupt handlers live in the module that owns the peripheral instead of
`stm32f1xx_it.c`:

* `USART1_IRQHandler` — `main.c`, latches one received byte;
* `TIM2_IRQHandler` — `stepper.c`, emits STEP pulses and counts them;
* `USART3_IRQHandler` — `legacy/scale_module.c` (not built).

## 2. Interrupt priorities (NVIC group 4, 4 pre-emption bits)

| Source | Priority | Reason |
|---|---|---|
| SysTick | 0 (`TICK_INT_PRIORITY`) | `HAL_GetTick()` must keep running |
| TIM2 | 1 | step pulses must not jitter |
| USART3 | 2 | legacy module only |
| USART1 | 5 | console bytes can wait |

Because the main loop can be blocked for up to ~100 ms inside one HX711
conversion, all time-critical motion work happens in the TIM2 interrupt: the
pulse counter is evaluated there, so a fixed-step move has a position error of
at most one step regardless of what the main loop is doing.

## 3. Step-pulse generation

```
Stepper_MoveStepsPps(steps, pps, dir)
        │  set DIR, enable driver
        │  ARR = 1e6 / pps - 1   (TIM2 counts at 1 MHz)
        │  EGR = UG  -> load ARR now, clear the update flag, counter = 0
        ▼
   HAL_TIM_Base_Start_IT(TIM2)
        │
        ▼  (every ARR ticks)
   TIM2_IRQHandler:
        STEP high ─ NOP delay ≈ 2.8 µs ─ STEP low
        move_count++
        if (move_count >= move_target) { stop timer; stop counting; keep torque }
```

* `Stepper_MoveBusy()` is polled by the main state machine; no busy-delay is
  used inside the state machine.
* After the move the driver stays enabled (`EN` low), so the rod holds its
  position. `Stepper_Disable()` (called by `MotorOff()`) releases it, which is
  what the manual workflow and command `d` rely on.

## 4. Run state machines

### 4.1 Automatic workflow (`a`)

```
IDLE ──a──► J_LIFT (110 mm @30 mm/s) ──► J_WAIT ──(≥20 g for 2 s)──► J_A (0.5 s)
                                              ▲                          │
                                              │  (bump < 2 s: restart)   │ average → A, motor off
                                              │                          ▼
   J_DONE ◄── J_RISE (40 mm up, silent) ◄── J_MAX (1 s peak) ◄── J_PRESS ◄── J_STABLE
                                                                  ▲          │
                                                       press_mm │          │ 1 s window ≤ 2 g
                                                       @1 mm/s  │          ▼
                                                                  └──── J_FALL (contact > A+20 g,
                                                                             150 ms blind time,
                                                                             10 s warning)
```

### 4.2 Manual workflow (`b`)

```
IDLE ──b──► K_WAIT (motor off, rod placed by hand) ──(≥20 g for 2 s)──► K_A (0.5 s)
                     ▲                                                       │ average → A, re-tare
                     │ (bump < 2 s: restart)                                 ▼
                  K_DONE ◄── K_MAX (1 s peak) ◄── K_PRESS (press_mm @1 mm/s)
                  (motor off)
```

Both workflows share the "object present" detector (`ST_J_WAIT` / `ST_K_WAIT`
are handled by the same `case`), which requires the weight to stay above 20 g
for 2 s; a shorter bump resets the timer, so banging the rod on the platform
does not start a measurement.

## 5. Why the guard rails exist

| Guard | Purpose |
|---|---|
| HX711 read timeout (120 ms) | a disconnected sensor cannot hang the firmware; the previous weight is held |
| `HX711_LastOk()` + 8-failure counter | one error message per failure burst, but the run continues |
| `HX711_TareHadEnough()` | the tare reports `zero` only when it really collected samples |
| stable-contact window (1 s, ≤ 2 g) | the pre-press zero is taken while the reading is quiet |
| peak window (1 s) | MAX is measured over a fixed period instead of a single sample |
| `stepper` position counting in the ISR | exact press depth even while the main loop is blocked on the sensor |
| `quiet_done` | suppresses 10 Hz streaming after `press done`, where the reading relaxes |
| command gating (`canRun` / `busy`) | a second start command during a run is ignored instead of corrupting the sequence |
| `c=` only in idle | the press depth cannot change in the middle of a run |

## 6. Timing budget of one automatic run

| Phase | Duration |
|---|---|
| tare | 5 valid samples at ~10 Hz ≈ 0.5 s |
| lift 110 mm | 8842 steps at 2412 pps ≈ 3.7 s |
| wait for the fruit | operator-dependent |
| A window | 0.5 s |
| free fall + contact | ≈ 0.5 – 1 s after the reading settles |
| stability window | ≥ 1 s |
| press 3 mm | 241 steps at 80 pps ≈ 3.0 s |
| MAX capture | 1.0 s |
| retract 40 mm | 3215 steps at 2412 pps ≈ 1.3 s |
| **total (without the wait)** | **≈ 11 s** |

## 7. Constants worth knowing

| Constant | File | Value |
|---|---|---|
| `HX711_XISHU` | `hx711.c` | 4268 (calibration) |
| `HX711_READ_TIMEOUT_MS` | `hx711.c` | 120 ms |
| `HX711_FILTER_LEN` | `hx711.c` | 5 (median) |
| `STEPS_OF` / `PPS_OF` | `main.c` | 80.386 steps/mm |
| `PPS_30`, `PPS_1` | `main.c` | 2412 pps, 80 pps |
| `STEPS_110MM`, `STEPS_40MM` | `main.c` | 8842, 3215 steps |
| `DETECT_G`, `DETECT_MS`, `CONTACT_G` | `main.c` | 20 g, 2000 ms, 20 g |
| `A_WIN_MS`, `STABLE_WIN_MS`, `STABLE_AMP_G` | `main.c` | 500 ms, 1000 ms, 2 g |
| `MAX_WIN_MS`, `FALL_WARN_MS` | `main.c` | 1000 ms, 10000 ms |
| `press_mm` | `main.c` | 3.0 mm, settable (0.1 – 20 mm) |
| `TIM_PRESCALER` | `stepper.h` | 72-1 (1 µs tick) |
| `MICROSTEPS` | `stepper.h` | 16 |
