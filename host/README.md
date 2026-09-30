# Host software (PC panel)

PyQt5 application that drives the tester over a serial port and plots the force
during a press.

```bash
pip install -r requirements.txt
python main.py
```

| File | Responsibility |
|---|---|
| `main.py` | window, buttons, force/time plot, state handling |
| `serial_device.py` | serial port handling and parsing of the ASCII protocol (Qt signals) |
| `ui_main.py` | widget layout |

The operator-facing labels are Chinese (the language of the lab this instrument
was built for); the code and its comments are English.

Full description: `../docs/05-pc-software.md`.
