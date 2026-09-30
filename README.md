# Orange-Press Tester

Firmware and PC software for a small laboratory instrument that measures the
**firmness of oranges (and similar citrus fruit)**.

A stepper-driven rod is pressed a configurable distance into the fruit while a
load cell records the force. The peak force reached during the press is the
firmness index. Two workflows are provided in one firmware image:

* **automatic mode (`a`)** – the rod is lifted, the rod falls freely onto the
  fruit, contact is detected from the load-cell reading, and the press starts
  automatically;
* **manual mode (`b`)** – the operator places the rod on the fruit by hand and
  the press starts as soon as a stable contact is detected.

Everything is controlled over a plain ASCII serial console, so the device works
with any terminal; a PyQt5 panel (`host/`) adds a force/time plot and buttons.

---

## Hardware at a glance

| Block | Part | Interface |
|---|---|---|
| Controller | STM32F103C8T6 (72 MHz, 64 KB flash / 20 KB RAM) | — |
| Stepper drive | HPD322, 16 microsteps, 42 mm stepper | PA2 = EN, PA3 = DIR, PA4 = PUL |
| Load cell ADC | HX711 | PC13 = DT, PC14 = SCK |
| Console | USB-TTL adapter | USART1, PA9 = TX, PA10 = RX, 115200 8N1 |

Mechanics: rack and pinion, one motor revolution moves 39.808 mm, which was
measured as **80.386 steps/mm**; the installed stroke is about 145 mm.

```
        PC / USB-TTL                STM32F103C8T6                 HPD322
   ┌────────────────────┐        ┌──────────────────┐        ┌──────────────┐
   │ serial terminal or │  TX/RX │  USART1  PA9/PA10│        │  EN  DIR  PUL│
   │ host/ PyQt5 panel  │◄──────►│                  │───────►│  (step drive)│
   └────────────────────┘        │  TIM2 ── pulse ──┼────────┘              │
                                 │                  │        └──────┬───────┘
                                 │  PC13/PC14 (GPIO)│               │ rod
                                 │        │         │               ▼
                                 │        ▼         │        rack and pinion
                                 │   HX711 ◄─ load cell ── press rod on fruit
                                 └──────────────────┘
```

> **Wiring warning:** PC13/PC14 belong to the backup domain of the STM32F103.
> `VBAT` must be connected to 3.3 V (or to a battery), otherwise those pins do
> not work and every weight reading stays 0.

---

## Serial commands

| Command | Meaning |
|---|---|
| `a` | start the automatic workflow |
| `b` | start the manual workflow |
| `d` | cut the motor and return to idle |
| `t` | idle self-test: up 20 mm, then back down 20 mm |
| `v` | print the internal state (useful when a run seems stuck) |
| `c=<mm>` + CR/LF | set the press depth, e.g. `c=2.5` (0.1 – 20 mm, default 3 mm) |

Representative output of one automatic run:

```
====== Orange-press merged: a=auto b=manual ======
commands: a=auto b=manual d=off c=<mm>
Current weight: 0.00 g

ready: a=auto b=manual d=off c=<mm> (press def 3mm)
a
zero
lift 110
place orange
W 122.45 g
A 124.31 g (5)
contact
press 3.00 mm
press done
sample done
MAX 271.86 g
done
```

The complete protocol (including every status line and the diagnostic output of
`v`) is documented in [`docs/03-serial-protocol.md`](docs/03-serial-protocol.md).

---

## Repository layout

```
.
├── README.md                  this file
├── docs/                      hardware, operation, protocol, build, PC software
│   ├── 01-hardware.md
│   ├── 02-operation.md
│   ├── 03-serial-protocol.md
│   ├── 04-build-and-flash.md
│   ├── 05-pc-software.md
│   ├── 06-architecture.md
│   ├── 07-history.md
│   └── manuals/               vendor manuals (driver, TOF sensor)
├── firmware/                  STM32Cube HAL project, Keil MDK-ARM target
│   ├── Src/ Inc/              application sources (commented in English)
│   ├── Drivers/               STM32F1xx HAL + the CMSIS files the build needs
│   ├── MDK-ARM/               F103.uvprojx (Keil uVision5, ARM Compiler 5)
│   ├── F103.ioc, .mxproject   STM32CubeMX configuration
│   └── legacy/                archived drivers that are not built
└── host/                      PyQt5 control panel (Python 3)
    ├── main.py                application and force/time plot
    ├── serial_device.py       serial link and protocol parsing
    └── ui_main.py             widget layout
```

---

## Quick start

**Firmware**

1. Open `firmware/MDK-ARM/F103.uvprojx` in Keil uVision5 (ARM Compiler 5) and
   build the `F103` target.
2. Flash the resulting `firmware/MDK-ARM/F103/F103.hex` over SWD (ST-Link or
   J-Link).
3. Open a serial terminal at 115200 8N1 and send a single character.

Details, including how to regenerate the project from `F103.ioc` with
STM32CubeMX, are in [`docs/04-build-and-flash.md`](docs/04-build-and-flash.md).

**PC panel**

```bash
cd host
pip install -r requirements.txt
python main.py
```

Select the COM port, open it, and use the *automatic*/*manual* buttons; the
press depth can be changed in the same window. See
[`docs/05-pc-software.md`](docs/05-pc-software.md).

---

## Calibration and tuning

The load-cell conversion factor, the steps-per-millimetre figure and every
detection threshold are compile-time constants with comments explaining them:

| Constant | File | Default |
|---|---|---|
| `HX711_XISHU` | `firmware/Src/hx711.c` | `4268` |
| `STEPS_OF` / `PPS_OF` (80.386 steps/mm) | `firmware/Src/main.c` | `80.386` |
| `DETECT_G`, `DETECT_MS` (object present) | `firmware/Src/main.c` | `20 g`, `2000 ms` |
| `CONTACT_G` (contact over the free weight) | `firmware/Src/main.c` | `20 g` |
| `MAX_WIN_MS` (peak-capture window) | `firmware/Src/main.c` | `1000 ms` |
| press depth `press_mm` | `firmware/Src/main.c` | `3.0 mm`, settable at run time |

Re-calibrate after replacing the load cell or the mechanics, see
[`docs/01-hardware.md`](docs/01-hardware.md).

---

## Third-party components and licensing

* `firmware/Drivers/STM32F1xx_HAL_Driver` – STMicroelectronics, BSD 3-Clause.
* `firmware/Drivers/CMSIS` – Arm Limited, Apache 2.0 (see `LICENSE.txt`).
* The vendor manuals in `docs/manuals/` remain the property of their
  manufacturers and are included for reference only; see
  [`docs/manuals/README.md`](docs/manuals/README.md).

The application code (firmware `Src/`, `Inc/`, `legacy/` and the Python panel)
was written for this project; no license has been chosen for it yet, so all
rights are reserved by the authors. Open an issue if you would like to reuse
it.
