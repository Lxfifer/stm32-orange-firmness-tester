# 03 - Serial protocol

* Physical layer: **USART1**, `PA9` = TX, `PA10` = RX, **115200 8N1**, no flow
  control.
* Character set: plain ASCII. Every line ends with `CR LF` (`\r\n`).
* Commands are case-insensitive: lower-case input is folded to upper case on
  reception.
* Weights are printed with two decimals, produced by integer arithmetic, so the
  output does not depend on float support in `printf`.

## 1. Commands

### 1.1 Single-character commands

They are only interpreted when they arrive as the first byte of a line.

| Byte | Name | Action | Refused when |
|---|---|---|---|
| `a` / `A` | automatic run | tare, lift 110 mm if needed, wait for the fruit, fall, contact, press, MAX, retract | a run is active |
| `b` / `B` | manual run | motor off, tare, wait for the hand-placed rod, average, press, MAX | a run is active |
| `d` / `D` | stop / idle | abort motion, disable the driver, return to idle | already idle |
| `t` / `T` | self-test | up 20 mm, pause 200 ms, down 20 mm | state is not idle or the rod is up |
| `v` / `V` | diagnostics | print the internal state | never |

`a`, `b` and `d` are echoed immediately as lower-case letters, **including when
the command is refused** (so a second `a` during a run still answers `a`).
`t` prints its own progress lines and `v` answers with a single status line.

### 1.2 Line command

```
c=<depth>[CR][LF]
```

* `<depth>` is a decimal number in millimetres, at most two decimals
  (`c=3`, `c=2.5`, `c=1.25`).
* Accepted range 0.1 – 20.0 mm; values outside are clamped.
* Only accepted while idle or after a finished run, otherwise the answer is
  `param ignored - set in idle only`.
* The leading `c=` must be the first two characters of the line; any other line
  answers with the help text.

## 2. Output lines

### 2.1 Banner and start-up

```
====== Orange-press merged: a=auto b=manual ======
commands: a=auto b=manual d=off c=<mm>
Current weight: 0.00 g

ready: a=auto b=manual d=off c=<mm> (press def 3mm)
```

If the HX711 does not answer, an extra line is printed before `Current weight`:

```
err scale no reply - check HX711 (power/VBAT/PC13-PC14 wiring)
```

### 2.2 Measurement data

| Line | Format | When |
|---|---|---|
| live weight | `W <g> g` | 10 Hz while waiting for the fruit, falling, stabilising and pressing (the manual workflow streams while waiting and while pressing) |
| free weight | `A <g> g (<n>)` | once, after the 0.5 s averaging window; `<n>` = number of samples averaged |
| peak force | `MAX <g> g` | once, after the 1 s capture window |

`<g>` always has exactly two decimals, e.g. `W 122.05 g`, `A 124.31 g (5)`,
`MAX 271.86 g`.

Streaming is deliberately silenced after `press done`: the reading relaxes
during the MAX window and would only confuse a live plot.

### 2.3 Progress and status lines

| Line | Meaning |
|---|---|
| `zero` | tare finished with valid samples |
| `lift 110` | automatic run: lifting 110 mm |
| `place orange` | automatic run: waiting for the fruit |
| `place rod on orange` | manual run: waiting for the hand-placed rod |
| `contact` | automatic run: contact detected during the free fall |
| `press <mm> mm` | pressing started, `<mm>` = current press depth |
| `press done` | the press move has finished |
| `sample done` | the MAX capture window has finished |
| `done` | run finished |
| `t` / `t up20` / `t ok` | self-test progress |
| `param <mm> mm` | new press depth accepted |
| `press`-related refusals | see the table in `docs/02-operation.md` |
| `state=<name> rod_up=<0/1> motor_busy=<0/1> scale_ok=<0/1> press=<mm> mm` | answer to `v` |
| `? a=auto b=manual d=off c=<mm>` | unknown command |

### 2.4 Error lines

| Line | Cause |
|---|---|
| `err scale no reply - check HX711 (power/VBAT/PC13-PC14 wiring)` | no valid HX711 sample during the tare, or 8 consecutive failed reads during a run |
| `err no contact (10s) - check orange` | the free fall did not touch anything within 10 s |
| `param ignored - set in idle only` | `c=` sent while a run was active |
| `t not allowed (need idle, rod low)` | self-test requested in a state that does not allow it |

## 3. State names reported by `v`

```
IDLE
J_LIFT   J_WAIT   J_A   J_FALL   J_STABLE   J_PRESS   J_MAX   J_RISE   J_DONE
K_WAIT   K_A      K_PRESS   K_MAX   K_DONE
```

The `J_*` states belong to the automatic workflow, the `K_*` states to the
manual one. The names come straight from the state enum in
`firmware/Src/main.c`.

Example:

```
v
state=J_WAIT rod_up=1 motor_busy=0 scale_ok=1 press=3.00 mm
```

* `rod_up = 1` – the rod is at the top and the motor is still holding it.
* `motor_busy = 1` – a fixed-step move is running.
* `scale_ok = 1` – the last HX711 read was answered.

## 4. Notes for host implementations

* Do not assume every line arrives in one read: use a line-buffered reader
  (the reference Python panel calls `readline()` in a 50 ms timer).
* Unknown lines should simply be displayed; the firmware only guarantees the
  formats listed above.
* Because `a`/`b`/`d` are echoed even when refused, a host that wants to know
  whether a run actually started should watch for `zero` / `lift 110` /
  `place orange` / `place rod on orange`.
