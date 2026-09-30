# 05 - PC software (host panel)

`host/` contains a small PyQt5 application that talks to the firmware over a
serial port. It is optional: any terminal at 115200 8N1 can drive the tester.

## 1. Requirements

```bash
cd host
pip install -r requirements.txt
python main.py
```

`requirements.txt`:

```
pyserial>=3.5
PyQt5>=5.15
pyqtgraph>=0.13
```

Tested with Python 3.13 on Windows 10/11.

## 2. What the window provides

| Area | Function |
|---|---|
| *串口设置* (serial) | COM-port list, refresh, open, close |
| *测试控制* (control) | **自动测试 a**, **手动测试 b**, **停止 d**, press depth + **设置 c=** |
| *检测数据* (read-out) | live weight, live force in N, fruit weight (A), peak force (MAX in g and N), state |
| plot | force versus time, 2 s sliding window, Y range 0–10 N (0–30 N after 5 s) |
| *串口通信日志* (log) | every line sent (`>>`) and received (`<<`) |
| status hint | port open/close feedback |

Conversion used for the display: `F[N] = m[g] × 0.00981`.

> The operator-facing labels are Chinese, because that is the language of the
> lab the instrument was built for. All code comments are English.

## 3. How the plot is filled

The curve intentionally covers **the pressing phase only**:

| Firmware line | Panel reaction |
|---|---|
| `press <mm> mm` | starts recording, clears the curve |
| `W <g> g` while recording | appends `(t, N)`; the last 2 s are kept |
| `press done` | stops recording, the finished curve stays on screen |
| `done`, `err ...`, or the command echo `a`/`b`/`d` | stops recording |

After `press done` the firmware stops streaming, because the reading relaxes
during the 1 s MAX window; plotting it would suggest a force drop that is not
physical.

## 4. Code layout

| File | Responsibility |
|---|---|
| `main.py` | application window, buttons, plot, state handling |
| `serial_device.py` | `SerialDevice`: port handling plus the ASCII protocol parser; exposes Qt signals `weight`, `base`, `max`, `state`, `send_log`, `recv_log` |
| `ui_main.py` | widget construction (`Ui_MainWindow.setupUi`) |

`SerialDevice.parse()` recognises exactly three data lines:

```python
"W 125.38 g"     -> weight_signal
"A 125.38 g (5)" -> base_signal
"MAX 268.42 g"   -> max_signal
# everything else -> state_signal
```

Any other program can reuse `serial_device.py` on its own; it has no UI
dependency besides `QObject`/`pyqtSignal`.

## 5. Typical session

1. Plug in the USB-TTL adapter, click *刷新串口*, select the port, click
   *打开串口*.
2. Type the wanted depth in the depth field and click *设置 c=*.
3. Place the fruit and click *自动测试 a* (or *手动测试 b*).
4. Watch the state field and the plot; the peak force is shown in *最大压力*.
5. *停止 d* releases the rod and returns to idle.
