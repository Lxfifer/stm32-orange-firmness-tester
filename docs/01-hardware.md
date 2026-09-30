# 01 - Hardware

## 1. Bill of materials

| # | Item | Notes |
|---|---|---|
| 1 | STM32F103C8T6 board ("blue pill" style, LQFP48) | 72 MHz, 64 KB flash, 20 KB RAM |
| 2 | HPD322 stepper driver | 3.3 V single-ended control inputs, 16 microsteps |
| 3 | 42 mm stepper motor (1.8°/step, 200 steps/rev) | mounted on the rack-and-pinion axis |
| 4 | Rack-and-pinion slide | about 145 mm usable stroke |
| 5 | Load cell + HX711 breakout | compression type, wired to the fruit platform |
| 6 | USB-TTL adapter (3.3 V) | console at 115200 8N1 |
| 7 | 12–24 V supply for the driver | according to the driver and motor rating |

## 2. Pin assignment

| STM32 pin | Direction | Connected to | Function in the firmware |
|---|---|---|---|
| PA2 | output | HPD322 `EN+` | enable, active **low** (`Stepper_Enable`) |
| PA3 | output | HPD322 `DIR+` | direction, low = rod **up** |
| PA4 | output (TIM2 pulse) | HPD322 `PUL+` | step pulses, ≥ 2.5 µs wide |
| PA9 | USART1_TX | USB-TTL RX | console output (also `printf`) |
| PA10 | USART1_RX | USB-TTL TX | console input |
| PC13 | GPIO in/out | HX711 `DT` (DOUT) | serial data from the ADC |
| PC14 | GPIO out | HX711 `SCK` (PD_SCK) | ADC clock |
| PA13/PA14 | SWD | ST-Link / J-Link | programming, JTAG is disabled |

The driver grounds (`EN-`, `DIR-`, `PUL-`) and the USB-TTL ground must be tied
to the board ground.

> **PC13/PC14 and VBAT.** These two pins are located in the backup domain of
> the STM32F103. They only work as GPIO while the backup domain is powered, so
> `VBAT` has to be connected to 3.3 V (or to a battery). With `VBAT` left
> floating the pins read as 0 and the HX711 driver times out on every single
> read - the firmware then prints
> `err scale no reply - check HX711 (power/VBAT/PC13-PC14 wiring)`.
> The HX711 itself must also be powered from 3.3 V and share ground.

The HPD322's own DIP switches select the microstep resolution; the firmware
assumes **16 microsteps** (`MICROSTEPS` in `firmware/Inc/stepper.h`).

## 3. Mechanical scaling (measured)

| Quantity | Value | How it was obtained |
|---|---|---|
| Motor steps per revolution | 200 | motor data sheet (1.8°/step) |
| Microsteps | 16 | HPD322 DIP switches |
| Steps per revolution | 3200 | 200 × 16 |
| Travel per motor revolution | 39.808 mm | pinion circumference (≈ π × 12.67 mm) |
| **Steps per millimetre** | **80.386** | measured by moving a known distance |
| Stroke used by the firmware | 110 mm up, 40 mm retract | `main.c` |
| Travel speed (fast) | 30 mm/s = 2412 pps | `PPS_30` |
| Press speed | 1 mm/s = 80 pps | `PPS_1` |

`STEPS_OF(mm)` and `PPS_OF(mm/s)` in `firmware/Src/main.c` use the 80.386
constant; a position error of at most one step is achieved because the pulse
count is evaluated inside the TIM2 interrupt.

## 4. Weighing front end (HX711)

* 24-bit two's-complement reading, channel A, gain 128 (25 clock pulses per
  conversion: 24 data bits plus one channel/gain pulse).
* `HX711_GetWeight()` performs **one** conversion per call, which gives about
  10 readings per second at 10 SPS.
* A 5-sample **sliding median filter** removes single-sample spikes.
* If `DOUT` does not go low within 120 ms the read is abandoned and the previous
  value is kept; `HX711_LastOk()` reports the failure to the main loop, which
  prints an error after 8 consecutive failures without aborting the run.

### Calibration

```c
/* firmware/Src/hx711.c */
#define HX711_XISHU   4268
...
w = (median - tare) * 0.01f * 0.00001f * HX711_XISHU;   /* grams */
```

Procedure after replacing the sensor or the platform:

1. Keep the platform empty, power up, and check that the firmware reports
   `zero` during the automatic tare.
2. Put a known mass (for example 1000 g) on the platform and note the displayed
   value `X`.
3. Compute `HX711_XISHU_new = HX711_XISHU_old * 1000 / X` and rebuild.

Since the firmware uses integer printing (`PrintW`) the resolution shown is
0.01 g even though the MCU runs MicroLIB.

## 5. Power-up checks

1. `Current weight: 0.00 g` appears after the banner. A constant 0.00 g with the
   `scale no reply` error means the HX711 is not answering (VBAT or wiring).
2. Send `t` while the rod rests on the bottom stop: the rod must move up 20 mm,
   pause, and return. If it moves the wrong way, swap the two motor phases or
   invert `DIR` logic in `Stepper_SetDir`.
3. Send `v` at any time to see `rod_up`, `motor_busy` and `scale_ok` without
   disturbing the run.
