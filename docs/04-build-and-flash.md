# 04 - Build and flash

## 1. Toolchain

| Tool | Version used | Notes |
|---|---|---|
| Keil MDK-ARM (uVision5) | 5.x | project `firmware/MDK-ARM/F103.uvprojx` |
| ARM Compiler | 5 (AC5, build 528) | the project targets the AC5 toolchain |
| STM32CubeMX | 6.1.1 | only needed to regenerate the peripheral code |
| STM32Cube FW_F1 | V1.8.4 | vendored in `firmware/Drivers` |
| Device | STM32F103C8 (`STM32F103xB`) | flash 64 KB, RAM 20 KB |

Debug adapter: ST-Link or J-Link over SWD (JTAG is disabled by
`__HAL_AFIO_REMAP_SWJ_NOJTAG()`).

## 2. Building

1. Open `firmware/MDK-ARM/F103.uvprojx`.
2. Select the `F103` target and press *Build* (`F7`).
3. The output goes to `firmware/MDK-ARM/F103/F103.hex` (and `.axf`).

Reference size of the current firmware (ARM Compiler 5, MicroLib, `-O3`):

```
Program Size: Code=9732 RO-data=360 RW-data=84 ZI-data=2236
Total RO 10092   Total RW 2320   Total ROM 10176
```

That is roughly 16 % of the 64 KB flash and 11 % of the 20 KB RAM, so there is
plenty of room for further features.

Build warnings: the original working copy produced three
`#1-D: last line of file ends without a newline` warnings (`Inc/usart.h` and
`Src/usart.c`). Final newlines were added in this repository, and the sources
were re-compiled with the same compiler as a check: **0 errors, 0 warnings**.

## 3. Flashing

* **uVision:** *Flash → Download* (`F8`) with the SWD adapter configured in the
  *Debug* tab of the target options.
* **Command line:** any SWD tool that accepts Intel HEX, e.g.

  ```
  STM32_Programmer_CLI -c port=SWD -w firmware/MDK-ARM/F103/F103.hex -rst
  ```

After reset the console prints the banner at 115200 8N1.

## 4. Regenerating the peripheral code with STM32CubeMX

`firmware/F103.ioc` reproduces the CubeMX configuration
(STM32CubeMX 6.1.1, STM32Cube FW_F1 V1.8.4, toolchain "MDK-ARM V5.27"):

* SYS: SWD, no JTAG;
* RCC: HSE crystal, PLL ×9 → 72 MHz;
* GPIO: only GPIOA/GPIOD clocks are enabled by the generator;
* USART1: 115200 8N1.

Everything else (TIM2, the HX711 GPIOs, the USART1 interrupt) is configured in
the application sources, so a regeneration must keep the user sections intact —
`main.c`, `gpio.c`, `usart.c`, `stm32f1xx_it.c` and `stm32f1xx_hal_msp.c` all
carry `USER CODE BEGIN/END` markers.

> Note: `sys.c` / `sys.h` in `firmware/Src` and `firmware/Inc` are CubeMX
> generators for the SWD remap, but they are **not** part of the Keil build —
> `HAL_MspInit()` in `stm32f1xx_hal_msp.c` performs the same remap.

## 5. Files that are tracked

* Application sources: `firmware/Src`, `firmware/Inc`.
* Vendored libraries: only the HAL files and the CMSIS headers the build needs.
  The STM32F0 drivers, the CMSIS DSP/NN libraries and the CMSIS core variants
  for other Cortex-M cores were removed, which shrinks the drivers from 115 MB
  to 6.5 MB. All include paths used by the project
  (`../Inc`, `../Drivers/STM32F1xx_HAL_Driver/Inc`,
  `.../Inc/Legacy`, `../Drivers/CMSIS/Device/ST/STM32F1xx/Include`,
  `../Drivers/CMSIS/Include`) are still satisfied, and the whole project was
  re-compiled and linked with ARM Compiler 5 to prove it.
* `firmware/MDK-ARM`: project files (`F103.uvprojx`, `F103.uvoptx`), the startup
  file, the debug configuration and the quick notes. Build output, listing
  files and the per-user `*.uvguix.*` layouts are ignored by `.gitignore`.
* `firmware/legacy`: archived drivers that are deliberately **not** compiled,
  see `firmware/legacy/README.md`.

## 6. Host software

The PyQt5 panel only needs Python 3 and three packages:

```bash
cd host
pip install -r requirements.txt
python main.py
```

See `docs/05-pc-software.md`.
