# 07 - History, provenance and the archived files

## 1. Evolution of the instrument

The tester was built up over several iterations. Only the last one is part of
the current Keil build; the earlier material is kept because it documents why
certain choices were made.

| Revision | Weighing front end | Motion | Console commands |
|---|---|---|---|
| motion tests | — | HPD322 / TB6600 / TMC2209 experiments | single letters |
| v2.0 (module version) | digital load-cell transmitter, custom TTL protocol on USART3 (PB10/PB11) @9600 | HPD322 through a lead screw | `a` = start/continue, `b` = pause, `c` = reset |
| **current (HX711 version)** | **HX711 on PC13/PC14, ~10 Hz** | HPD322, rack and pinion, 80.386 steps/mm | `a` automatic, `b` manual, `d` stop, `t` self-test, `v` diagnostics, `c=<mm>` depth |

Changes that came with the current revision:

* the transmitter module was replaced by a direct HX711 front end, so the
  firmware no longer has to speak the module's binary protocol;
* the press depth became a run-time parameter (`c=<mm>`) instead of a fixed
  1 mm move;
* the automatic and the manual workflow were merged into a single image, so one
  board can be used for both measurement styles;
* free-fall contact detection was added for the automatic workflow (the rod is
  released and its own weight presses it onto the fruit).

The earlier `压橘控制流程说明.md` (v2.0) and `压橘装置升级版技术文档.md` (v2.3
draft) documents are **not** reproduced here because they describe command
semantics that no longer exist. Use `docs/02-operation.md` and
`docs/03-serial-protocol.md` for the behaviour of the firmware in this
repository.

## 2. Archived files in `firmware/legacy/`

| File | What it is | Why it is kept |
|---|---|---|
| `scale_module.c` / `.h` | HAL port of the digital load-cell transmitter driver (USART3, hex frames with XOR checksum, baud-rate switching) | needed if the transmitter module ever comes back, and it documents the module protocol |
| `oled.c` / `.h`, `oledfont.h` | 128×64 SSD1306 driver, 4-wire software SPI | the pins (PB8/PB9/PB13/PB14) do not clash with the current wiring, so a local display can be added later |

None of these files is listed in `F103.uvprojx`, so they are neither compiled
nor linked. `oled.h` still contains two StdPeriph macros (`GPIO_ResetBits`,
`GPIO_SetBits`) for a test LED that is never switched; they are unused and
would have to be converted to HAL calls before the file could be added to the
build.

The HX711 driver in `Src/hx711.c` was ported from a standard-peripheral
load-cell project (`电子秤程序-HX711`), which is why the calibration formula
`(raw - tare) * 0.01 * 0.00001 * XISHU` and the factor 4268 still look the way
they do.

The VL53L0X (laser time-of-flight) documentation in `docs/manuals` belongs to a
ranging sensor that was evaluated during development but is not used by this
firmware.

## 3. Historical notes of the module-based revision (translated)

The original `MDK-ARM\实验说明.TXT` was written for the v2.0 hardware. Kept here
for reference, translated and with the obsolete details marked:

```
1. WIRING
   Stepper driver HPD322 (control signals):
       board PA2 ---- driver EN+    (enable, active low)
       board PA3 ---- driver DIR+   (direction: low = up, high = down; measured)
       board PA4 ---- driver PUL+   (pulse)
       board GND ---- driver EN-/DIR-/PUL- (signal ground)

   Digital load-cell transmitter (custom TTL protocol, USART3 @9600):
       board PB10 --- module RXD    (board transmits -> module receives)
       board PB11 --- module TXD    (module transmits -> board receives)
       board 3.3/5V - module VCC
       board GND ---- module GND
       note: module parameters AD = 40 Hz, avg = 1, confirmed/set automatically
             at start-up
       calibration: grams = module net count x 0.00979
                    (re-calibrate after replacing the sensor)

   Console (SSCOM, 115200 8N1; single characters are enough):
       board PA9  ---- USB-TTL RX
       board PA10 ---- USB-TTL TX
       board GND  ---- USB-TTL GND
       commands: a = start/continue, b = pause (keep torque),
                 c = reset / release (rod drops to the bottom)

2. EXPERIMENT (orange press test)
   Rack-and-pinion scaling, measured: 80.386 steps/mm, 145 mm stroke
   = 11656 steps.
   After power-up/reset the rod falls to the bottom stop; once the console
   greets, send 'a':
       STEP 1  tare the module
       STEP 2  return to the top, 11656 steps @30 RPM
       STEP 3  place the orange (3 s)
       STEP 4  3 s sampling of the initial weight W0
       STEP 5  controlled descent @30 RPM, reading change > W0 + 10 g means
               contact (the travelled step count is recorded)
               after contact: stop for 3 s and tare again
       STEP 6  press 1 mm (80 steps) @2 RPM, then 2 s sampling of F1
               (increase relative to the pre-press value)
       STEP 7  print RESULT (W0 / F1)
       STEP 8  return to the top by the recorded descent step count @30 RPM,
               hold until 'c'; 'c' releases and the rod drops to the bottom
   Serial output at 20 Hz: W:xx.xxg RPM:xx ST:state (weight with 2 decimals)
```

Differences from the firmware in this repository: the module protocol is gone
(HX711 instead), the flow no longer returns to the top after a measurement (the
rod simply retracts 40 mm and stays there), the press depth is configurable, and
the manual workflow exists as an alternative.
