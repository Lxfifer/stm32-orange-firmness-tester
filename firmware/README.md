# Firmware

STM32Cube HAL project for the STM32F103C8T6, built with Keil MDK-ARM
(ARM Compiler 5, MicroLib).

```
firmware/
├── Src/                 application sources (English comments)
├── Inc/                 matching headers
├── Drivers/             STM32F1xx HAL + the CMSIS files the build needs
├── MDK-ARM/             F103.uvprojx / F103.uvoptx (uVision5 project)
├── F103.ioc, .mxproject STM32CubeMX configuration (CubeMX 6.1.1, FW_F1 V1.8.4)
└── legacy/              drivers that are archived and not compiled
```

## Build

1. Open `MDK-ARM/F103.uvprojx` in Keil uVision5.
2. Build the `F103` target → `MDK-ARM/F103/F103.hex`.
3. Flash over SWD.

Reference size: `Code=9732 RO-data=360 RW-data=84 ZI-data=2236`
(10176 bytes of flash, about 16 % of the 64 KB available).

## Which files end up in the image

Compiled: `mdk-arm/startup_stm32f103xb.s`, `Src/main.c`, `Src/usart.c`,
`Src/gpio.c`, `Src/stepper.c`, `Src/hx711.c`, `Src/stm32f1xx_it.c`,
`Src/stm32f1xx_hal_msp.c`, `Src/system_stm32f1xx.c`, and the HAL sources listed
in the project.

Not compiled (kept for reference): `Src/sys.c`, everything in `legacy/`.

## Interrupt handlers

`USART1_IRQHandler` lives in `Src/main.c` and `TIM2_IRQHandler` in
`Src/stepper.c`; the CubeMX-generated `stm32f1xx_it.c` only contains the
Cortex-M core handlers.

## Entry points into the code

| What you want to change | Where |
|---|---|
| detection thresholds, speeds, press depth default | `Src/main.c`, the `#define`/constants block at the top |
| state machine, console commands | `Src/main.c`, `RunState()` / `HandleCmd()` / `HandleLine()` |
| load-cell calibration and filtering | `Src/hx711.c` |
| microsteps, pulse timing, motion API | `Src/stepper.c`, `Inc/stepper.h` |
| console port settings | `Src/usart.c`, `Src/stm32f1xx_hal_msp.c` |

Further reading: `../docs/06-architecture.md`.
