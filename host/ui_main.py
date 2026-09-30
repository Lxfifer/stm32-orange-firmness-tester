"""Qt widget layout of the orange-press control panel.

The widgets are created by hand instead of Qt Designer; every attribute the
main window touches (portBox, openBtn, ...) is created here. The operator-facing
labels are Chinese, matching the lab the tool was built for.
"""

from PyQt5.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QComboBox,
    QLineEdit,
    QTextEdit,
    QGroupBox
)


class Ui_MainWindow:

    def setupUi(self, window):

        window.setWindowTitle("压橘硬度检测系统")
        window.resize(1200, 800)

        # top-level layout
        self.mainLayout = QVBoxLayout()

        # =========================
        # Serial port settings
        # =========================
        serialGroup = QGroupBox("串口设置")
        serialLayout = QHBoxLayout()

        self.portBox = QComboBox()
        self.refreshBtn = QPushButton("刷新串口")
        self.openBtn = QPushButton("打开串口")
        self.closeBtn = QPushButton("关闭串口")

        serialLayout.addWidget(QLabel("COM:"))
        serialLayout.addWidget(self.portBox)
        serialLayout.addWidget(self.refreshBtn)
        serialLayout.addWidget(self.openBtn)
        serialLayout.addWidget(self.closeBtn)

        serialGroup.setLayout(serialLayout)
        self.mainLayout.addWidget(serialGroup)

        # =========================
        # Test control
        # =========================
        controlGroup = QGroupBox("测试控制")
        controlLayout = QHBoxLayout()

        self.autoBtn = QPushButton("自动测试 a")
        self.manualBtn = QPushButton("手动测试 b")
        self.stopBtn = QPushButton("停止 d")

        controlLayout.addWidget(self.autoBtn)
        controlLayout.addWidget(self.manualBtn)
        controlLayout.addWidget(self.stopBtn)

        # press depth
        controlLayout.addWidget(QLabel("下压深度(mm)"))

        self.depthEdit = QLineEdit()
        self.depthEdit.setText("3.0")
        self.depthEdit.setFixedWidth(80)

        self.setDepthBtn = QPushButton("设置 c=")

        controlLayout.addWidget(self.depthEdit)
        controlLayout.addWidget(self.setDepthBtn)

        controlGroup.setLayout(controlLayout)
        self.mainLayout.addWidget(controlGroup)

        # =========================
        # Measurement read-out
        # =========================
        dataGroup = QGroupBox("检测数据")
        dataLayout = QHBoxLayout()

        self.currentLabel = QLabel("当前重量:0 g")
        self.currentNLabel = QLabel("当前压力:0 N")
        self.fruitLabel = QLabel("果实重量:0 g")
        self.maxLabel = QLabel("最大压力:0 g")
        self.stateLabel = QLabel("状态:等待")

        dataLayout.addWidget(self.currentLabel)
        dataLayout.addWidget(self.currentNLabel)
        dataLayout.addWidget(self.fruitLabel)
        dataLayout.addWidget(self.maxLabel)
        dataLayout.addWidget(self.stateLabel)

        dataGroup.setLayout(dataLayout)
        self.mainLayout.addWidget(dataGroup)

        # =========================
        # Plot area (filled by main.py)
        # =========================
        self.plotWidget = QWidget()
        self.plotWidget.setMinimumHeight(350)
        self.mainLayout.addWidget(self.plotWidget)

        # =========================
        # Serial log
        # =========================
        logGroup = QGroupBox("串口通信日志")
        logLayout = QHBoxLayout()

        # sent frames
        sendLayout = QVBoxLayout()
        sendLayout.addWidget(QLabel("发送"))

        self.sendText = QTextEdit()
        self.sendText.setReadOnly(True)
        sendLayout.addWidget(self.sendText)

        # received frames
        recvLayout = QVBoxLayout()
        recvLayout.addWidget(QLabel("接收"))

        self.recvText = QTextEdit()
        self.recvText.setReadOnly(True)
        recvLayout.addWidget(self.recvText)

        logLayout.addLayout(sendLayout)
        logLayout.addLayout(recvLayout)

        logGroup.setLayout(logLayout)
        self.mainLayout.addWidget(logGroup)

        # =========================
        # Status hint
        # =========================
        self.tipLabel = QLabel("设备未连接")
        self.mainLayout.addWidget(self.tipLabel)

        window.setLayout(self.mainLayout)
