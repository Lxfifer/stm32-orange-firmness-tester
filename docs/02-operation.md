# 02 - Operation

Everything is driven from a serial terminal (115200 8N1). Commands are single
characters and are case-insensitive; `a`, `b` and `d` are echoed back in lower
case as soon as they are received, **even when the command is refused** because
a run is already in progress.

## 1. Automatic workflow (`a`)

Use it when the rod may fall freely onto the fruit. The whole sequence is
automatic after the fruit has been placed.

| Step | Firmware action | Serial output |
|---|---|---|
| 1 | tare with the platform empty; report a dead sensor instead of hanging | `zero` (or the `err scale ...` line) |
| 2 | if the rod is not already up: lift 110 mm at 30 mm/s (8842 steps, ≈3.6 s) | `lift 110`, then `place orange` |
| 3 | wait for the fruit. The weight must stay above **20 g for 2 s** (a shorter bump restarts the timer) | `W <g> g` streamed at 10 Hz |
| 4 | average the weight over **0.5 s** → that is the free weight `A`; then cut the motor so the rod falls under its own weight | `A <g> g (n)` with `n` = number of samples |
| 5 | ignore the first **150 ms** of the fall, then detect contact when the reading exceeds `A + 20 g`; if nothing happens within **10 s**, warn once | `contact`, or `err no contact (10s) - check orange` |
| 6 | collect readings until a **1 s window varies by at most 2 g**; then **re-tare** (this is the pre-press zero) and press `press_mm` downwards at 1 mm/s | `press <mm> mm` at the start of the move |
| 7 | stream the reading while pressing | `W <g> g` at 10 Hz until the move ends, then `press done` |
| 8 | record the maximum over **1 s** | `sample done`, `MAX <g> g` |
| 9 | retract 40 mm at 30 mm/s **silently** (no streaming during the retract) | `done` |

After step 9 the rod stays up with the motor energised, so the next `a` run
starts directly with `place orange`. Send `d` (or start a manual run, which
switches the motor off) to release it.

Timing constants, all in `firmware/Src/main.c`:

| Constant | Value | Meaning |
|---|---|---|
| `DETECT_G` / `DETECT_MS` | 20 g / 2000 ms | "the fruit is on the platform" |
| `A_WIN_MS` | 500 ms | averaging window for the free weight |
| `FALL_IGNORE_MS` | 150 ms | blind time at the beginning of the fall |
| `CONTACT_G` | 20 g | contact above the free weight |
| `STABLE_WIN_MS` / `STABLE_AMP_G` | 1000 ms / 2 g | stability criterion before pressing |
| `MAX_WIN_MS` | 1000 ms | peak-force capture window |
| `FALL_WARN_MS` | 10000 ms | "no contact" warning |
| `PRINT_MS` | 100 ms | streaming period (10 Hz) |

## 2. Manual workflow (`b`)

Use it when the rod has to be placed on the fruit by hand.

1. The motor is switched off and the rod can be moved freely.
2. The firmware tares (`zero`) and asks for the rod: `place rod on orange`.
3. The operator lowers the rod until it rests on the fruit. When the weight
   stays above **20 g for 2 s**, the measurement starts.
4. The weight is averaged over 0.5 s (`A <g> g (n)`), the scale is re-tared and
   the rod presses `press_mm` at 1 mm/s (`press <mm> mm`).
5. `press done` → 1 s maximum capture → `sample done`, `MAX <g> g`, `done`.
6. The motor is switched off again, so the rod can be lifted by hand.

The manual workflow does **not** retract automatically and does **not** use the
free-fall contact detection: contact is whatever the operator places on the
fruit, and the "pre-press zero" is taken right before the press moves.

## 3. Commands common to both workflows

| Command | Behaviour |
|---|---|
| `d` | stops any motion, disables the driver and returns to idle (ignored when already idle) |
| `v` | prints `state=... rod_up=... motor_busy=... scale_ok=... press=... mm` |
| `t` | idle self-test: up 20 mm, 200 ms pause, down 20 mm. Refused unless the state is idle and the rod is at the bottom |
| `c=<mm>` + CR/LF | sets the press depth for the next run; accepted only in idle or after a finished run, clamped to 0.1 – 20 mm |

The press depth is reported back as `param <mm> mm`; a rejected line answers
`param ignored - set in idle only`.

## 4. Message reference

| Message | Meaning / what to do |
|---|---|
| `ready: a=auto b=manual d=off c=<mm> (press def 3mm)` | idle, ready for a command |
| `zero` | tare finished with valid samples |
| `err scale no reply - check HX711 (power/VBAT/PC13-PC14 wiring)` | HX711 not answering: check VBAT, 3.3 V, PC13/PC14 wiring |
| `lift 110` / `place orange` | automatic run, rod moving up / waiting for the fruit |
| `place rod on orange` | manual run, put the rod on the fruit |
| `A <g> g (n)` | free weight before the press, `n` samples averaged |
| `contact` | free fall detected the fruit |
| `err no contact (10s) - check orange` | nothing was hit during the fall; check that the fruit is under the rod |
| `press <mm> mm` / `press done` | start / end of the pressing move |
| `sample done` / `MAX <g> g` | peak-force window finished and its result |
| `done` | run finished |
| `param <mm> mm` | new press depth accepted |
| `param ignored - set in idle only` | `c=` was sent while a run was active |
| `t up20` / `t ok` | self-test progress |
| `t not allowed (need idle, rod low)` | self-test refused |
| `? a=auto b=manual d=off c=<mm>` | unknown command |

## 5. Typical measurement session

1. Power up, check `Current weight: 0.00 g` and that the `scale no reply` error
   does not appear.
2. Send `c=3.0` (or the depth of interest) while the tester is idle.
3. Put the fruit under the rod and send `a`.
4. Wait for `MAX ... g` / `done`, then remove the fruit and optionally send `d`
   to release the rod.
5. Repeat from step 3 as often as required.
