"""Serial link to the orange-press tester.

The class wraps pyserial and turns the ASCII line protocol of the firmware
(see docs/03-serial-protocol.md) into Qt signals, so the GUI can stay free of
any parsing logic.

Received lines are parsed as follows:

    W 125.38 g      -> weight_signal   (live weight while a run is in progress)
    A 125.38 g (5)  -> base_signal     (weight sampled before the press)
    MAX 268.42 g    -> max_signal      (peak force of the press)
    anything else   -> state_signal    (status text, errors, prompts)
"""

import serial
import serial.tools.list_ports

from PyQt5.QtCore import QObject, pyqtSignal


class SerialDevice(QObject):

    # =========================
    # Data handed to the main window
    # =========================

    # live weight in g
    weight_signal = pyqtSignal(float)

    # reference weight A in g
    base_signal = pyqtSignal(float)

    # peak force in g
    max_signal = pyqtSignal(float)

    # status text
    state_signal = pyqtSignal(str)

    # =========================
    # Serial log
    # =========================

    # something was sent
    send_log_signal = pyqtSignal(str)

    # something was received
    recv_log_signal = pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self.ser = None

    # List the available COM ports.
    @staticmethod
    def get_ports():
        ports = []
        for p in serial.tools.list_ports.comports():
            ports.append(p.device)
        return ports

    # Open the serial port.
    def open(self, port):
        # If the same port is already open, report success straight away so a
        # second click does not produce a bogus "access denied" message.
        if self.ser is not None and self.ser.is_open:
            if self.ser.portstr.upper() == port.upper():
                return True
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None

        try:
            self.ser = serial.Serial(
                port=port,
                baudrate=115200,
                bytesize=8,
                parity='N',
                stopbits=1,
                timeout=0.05
            )
            return True
        except Exception as e:
            self.recv_log_signal.emit(f"串口打开失败:{e}")
            return False

    # Close the serial port.
    def close(self):
        if self.ser:
            self.ser.close()
            self.ser = None

    # =========================
    # Transmit
    # =========================

    def send(self, text):
        if self.ser is None:
            return

        try:
            self.ser.write(text.encode("ascii"))

            # echo into the send log, with CR/LF made visible
            show = text.replace("\r\n", "\\r\\n")
            self.send_log_signal.emit(">> " + show)
        except Exception as e:
            self.send_log_signal.emit(f"发送错误:{e}")

    # =========================
    # Receive
    # =========================

    def read_data(self):
        """Drain everything the port has buffered (called by a QTimer)."""
        if self.ser is None:
            return

        while self.ser.in_waiting:
            try:
                data = self.ser.readline()
                msg = data.decode("ascii", errors="ignore").strip()

                if msg:
                    self.recv_log_signal.emit("<< " + msg)
                    self.parse(msg)
            except Exception:
                pass

    # =========================
    # Protocol parsing
    # =========================

    def parse(self, msg):

        # -----------------
        # live weight
        # W 125.38 g
        # -----------------
        if msg.startswith("W "):
            try:
                value = float(
                    msg.replace("W", "")
                       .replace("g", "")
                       .strip()
                )
                self.weight_signal.emit(value)
            except:
                pass

        # -----------------
        # reference weight
        # A 125.38 g (5)
        # -----------------
        elif msg.startswith("A "):
            try:
                value = float(msg.split()[1])
                self.base_signal.emit(value)
            except:
                pass

        # -----------------
        # peak force
        # MAX 268.42 g
        # -----------------
        elif msg.startswith("MAX"):
            try:
                value = float(msg.split()[1])
                self.max_signal.emit(value)
            except:
                pass

        # -----------------
        # every other status line
        # -----------------
        else:
            self.state_signal.emit(msg)
