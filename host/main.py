"""PC-side control panel for the orange-press tester.

Run it with

    python main.py

The window talks to the firmware over a serial port at 115200 8N1 and offers
the same commands as a plain terminal (a = automatic, b = manual, d = stop,
c=<mm> = press depth). While a press is running it plots force in newtons
against time; the curve is only recorded between the "press <mm> mm" event and
the "press done" event, because the reading relaxes during the 1 s MAX capture
window and would distort the plot.

Conversion used for the display: F[N] = m[g] * 0.00981
"""

import sys
import time

import pyqtgraph as pg

from PyQt5.QtWidgets import QApplication, QWidget
from PyQt5.QtCore import QTimer

from ui_main import Ui_MainWindow
from serial_device import SerialDevice


class MainWindow(QWidget):

    def __init__(self):
        super().__init__()

        # ======================
        # Build the user interface
        # ======================
        self.ui = Ui_MainWindow()
        self.ui.setupUi(self)

        # ======================
        # Serial port object
        # ======================
        self.device = SerialDevice()
        self.refresh_ports()

        # ======================
        # Force/time curve
        # ======================
        self.plot = pg.PlotWidget()

        self.plot.setLabel("left", "Force", units="N")
        self.plot.setLabel("bottom", "Time", units="s")

        self.plot.showGrid(x=True, y=True)

        # start with a 0..10 N range
        self.plot.setYRange(0, 10)

        self.curve = self.plot.plot(pen=pg.mkPen(width=2))

        self.ui.plotWidget.setLayout(self._new_layout())
        self.ui.plotWidget.layout().addWidget(self.plot)

        # data buffers
        self.x = []
        self.y = []

        self.start_time = time.time()

        # True while the "pressing" window should be recorded: set on the
        # "press ..." event, cleared on "done"/"err".
        self.recording = False

        # ======================
        # Serial polling timer
        # ======================
        self.timer = QTimer()
        self.timer.timeout.connect(self.device.read_data)
        self.timer.start(50)

        # ======================
        # Signal connections
        # ======================
        self.device.weight_signal.connect(self.update_weight)
        self.device.base_signal.connect(self.update_base)
        self.device.max_signal.connect(self.update_max)
        self.device.state_signal.connect(self.update_state)
        self.device.send_log_signal.connect(self.show_send)
        self.device.recv_log_signal.connect(self.show_receive)

        # ======================
        # Buttons
        # ======================
        self.ui.refreshBtn.clicked.connect(self.refresh_ports)
        self.ui.openBtn.clicked.connect(self.open_serial)
        self.ui.closeBtn.clicked.connect(self.close_serial)
        self.ui.autoBtn.clicked.connect(self.auto_test)
        self.ui.manualBtn.clicked.connect(self.manual_test)
        self.ui.stopBtn.clicked.connect(self.stop_test)
        self.ui.setDepthBtn.clicked.connect(self.set_depth)

    # Create the layout that holds the plot.
    def _new_layout(self):
        from PyQt5.QtWidgets import QVBoxLayout
        return QVBoxLayout()

    # ======================
    # Serial port
    # ======================

    def refresh_ports(self):
        self.ui.portBox.clear()
        self.ui.portBox.addItems(self.device.get_ports())

    def open_serial(self):
        port = self.ui.portBox.currentText()

        if self.device.open(port):
            self.ui.tipLabel.setText(f"{port} 已打开")
        else:
            self.ui.tipLabel.setText(
                f"{port} 打开失败：请确认没有其它程序占用该串口，或拔插USB转TTL后再试"
            )

    def close_serial(self):
        self.device.close()
        self.ui.tipLabel.setText("串口关闭")

    # ======================
    # Sending commands
    # ======================

    def auto_test(self):
        """Start the automatic workflow (command 'a')."""
        self.recording = False
        self.clear_curve()
        self.device.send("a")

    def manual_test(self):
        """Start the manual workflow (command 'b')."""
        self.recording = False
        self.clear_curve()
        self.device.send("b")

    def stop_test(self):
        """Stop and return to idle (command 'd')."""
        self.device.send("d")

    def set_depth(self):
        """Send the press depth as a line command, e.g. 'c=2.5'."""
        depth = self.ui.depthEdit.text()
        cmd = ("c=" + depth + "\r\n")
        self.device.send(cmd)

    # ======================
    # Data updates
    # ======================

    # "W 125.38 g" -> live weight
    def update_weight(self, g):

        N = g * 0.00981

        self.ui.currentLabel.setText(f"当前重量:{g:.2f} g")
        self.ui.currentNLabel.setText(f"当前压力:{N:.2f} N")

        # Only the pressing window is plotted.
        if not self.recording:
            return

        t = time.time() - self.start_time

        self.x.append(t)
        self.y.append(N)

        # keep the last 2 seconds
        while len(self.x) > 0:
            if t - self.x[0] > 2:
                self.x.pop(0)
                self.y.pop(0)
            else:
                break

        self.curve.setData(self.x, self.y)
        self.plot.setXRange(max(0, t - 2), t)

        # widen the Y axis after 5 s
        if t > 5:
            self.plot.setYRange(0, 30)

    # "A 125.38 g" -> reference weight before the press
    def update_base(self, g):
        self.ui.fruitLabel.setText(f"果实重量:{g:.2f} g")

    # "MAX 268.42 g" -> peak force
    def update_max(self, g):

        N = g * 0.00981

        self.ui.maxLabel.setText(f"最大压力:{g:.2f} g / {N:.2f} N")

    # Status lines
    def update_state(self, msg):

        # "press 3.00 mm": the tare is done and the rod starts moving down, so
        # this is the start of the curve, which only covers the pressing phase.
        if msg.startswith("press ") and msg.endswith(" mm"):
            self.recording = True
            self.clear_curve()
            self.ui.stateLabel.setText("状态:下压中(" + msg + ")")
            return

        # Bottom of the press = end of the curve (the reading relaxes after
        # this, so it is no longer plotted).
        if msg == "press done":
            self.recording = False
            self.ui.stateLabel.setText("状态:下压到位(曲线终点)")
            return

        # End of a run / error / new command: stop recording but keep the curve
        # on screen so it can still be inspected.
        if msg == "done" or msg.startswith("err ") or msg in ("a", "b", "d"):
            self.recording = False

        self.ui.stateLabel.setText("状态:" + msg)

    # ======================
    # Log windows
    # ======================

    def show_send(self, msg):
        self.ui.sendText.append(msg)

    def show_receive(self, msg):
        self.ui.recvText.append(msg)

    # ======================
    # Curve helpers
    # ======================

    def clear_curve(self):
        self.x.clear()
        self.y.clear()
        self.curve.clear()
        self.start_time = time.time()


if __name__ == "__main__":

    app = QApplication(sys.argv)

    win = MainWindow()
    win.show()

    sys.exit(app.exec_())
