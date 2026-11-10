from PyQt6 import QtCore, QtWidgets


class Ui_MainWindow:
    def setupUi(self, MainWindow):
        MainWindow.setObjectName("MainWindow")
        MainWindow.resize(1420, 1000)
        MainWindow.setWindowTitle("Tarea 2 · BME688")
        MainWindow.setMinimumSize(1080, 760)

        self.central_widget = QtWidgets.QWidget(parent=MainWindow)
        root_layout = QtWidgets.QVBoxLayout(self.central_widget)
        root_layout.setContentsMargins(14, 12, 14, 12)

        connection_layout = QtWidgets.QHBoxLayout()
        connection_group = QtWidgets.QGroupBox("Conexión serial")
        connection_form = QtWidgets.QGridLayout(connection_group)
        self.port_combo = QtWidgets.QComboBox()
        self.port_combo.setMinimumWidth(170)
        self.refresh_ports_button = QtWidgets.QPushButton("Actualizar puertos")
        self.usb_filter_check = QtWidgets.QCheckBox("Filtrar adaptadores USB-serial comunes")
        self.usb_filter_check.setChecked(True)
        self.baudrate_combo = QtWidgets.QComboBox()
        self.connect_button = QtWidgets.QPushButton("Conectar")
        self.disconnect_button = QtWidgets.QPushButton("Desconectar")
        self.connection_status = QtWidgets.QLabel("Desconectado")
        self.connection_status.setWordWrap(True)
        connection_form.addWidget(QtWidgets.QLabel("Puerto:"), 0, 0)
        connection_form.addWidget(self.port_combo, 0, 1)
        connection_form.addWidget(self.refresh_ports_button, 0, 2)
        connection_form.addWidget(self.usb_filter_check, 1, 0, 1, 3)
        connection_form.addWidget(QtWidgets.QLabel("Velocidad:"), 2, 0)
        connection_form.addWidget(self.baudrate_combo, 2, 1)
        connection_form.addWidget(self.connect_button, 3, 0)
        connection_form.addWidget(self.disconnect_button, 3, 1)
        connection_form.addWidget(self.connection_status, 4, 0, 1, 3)
        connection_layout.addWidget(connection_group, 1)

        acquisition_group = QtWidgets.QGroupBox("Adquisición y transmisión")
        acquisition_form = QtWidgets.QGridLayout(acquisition_group)
        self.frequency_combo = QtWidgets.QComboBox()
        self.temperature_check = QtWidgets.QCheckBox("Temperatura (T)")
        self.humidity_check = QtWidgets.QCheckBox("Humedad (H)")
        self.pressure_check = QtWidgets.QCheckBox("Presión (P)")
        for checkbox in (self.temperature_check, self.humidity_check, self.pressure_check):
            checkbox.setChecked(True)
        self.temperature_transmission = QtWidgets.QLabel("Transmitida")
        self.humidity_transmission = QtWidgets.QLabel("Transmitida")
        self.pressure_transmission = QtWidgets.QLabel("Transmitida")
        acquisition_form.addWidget(QtWidgets.QLabel("Frecuencia común:"), 0, 0)
        acquisition_form.addWidget(self.frequency_combo, 0, 1)
        acquisition_form.addWidget(self.temperature_check, 1, 0)
        acquisition_form.addWidget(self.temperature_transmission, 1, 1)
        acquisition_form.addWidget(self.humidity_check, 2, 0)
        acquisition_form.addWidget(self.humidity_transmission, 2, 1)
        acquisition_form.addWidget(self.pressure_check, 3, 0)
        acquisition_form.addWidget(self.pressure_transmission, 3, 1)
        self.apply_button = QtWidgets.QPushButton("Aplicar configuración")
        self.start_button = QtWidgets.QPushButton("Iniciar")
        self.pause_button = QtWidgets.QPushButton("Pausar")
        actions = QtWidgets.QHBoxLayout()
        actions.addWidget(self.apply_button)
        actions.addWidget(self.start_button)
        actions.addWidget(self.pause_button)
        acquisition_form.addLayout(actions, 4, 0, 1, 2)
        connection_layout.addWidget(acquisition_group, 1)

        processing_group = QtWidgets.QGroupBox("Procesamiento en ESP32")
        processing_form = QtWidgets.QGridLayout(processing_group)
        self.average_check = QtWidgets.QCheckBox("Promedio móvil de temperatura")
        self.average_check.setChecked(False)
        self.window_combo = QtWidgets.QComboBox()
        self.peak_check = QtWidgets.QCheckBox("Detección de máximos locales")
        self.peak_check.setChecked(False)
        self.threshold_spin = QtWidgets.QDoubleSpinBox()
        self.threshold_spin.setSuffix(" °C")
        self.confirmed_threshold = QtWidgets.QLabel("Umbral confirmado: --")
        processing_form.addWidget(self.average_check, 0, 0, 1, 2)
        processing_form.addWidget(QtWidgets.QLabel("Ventana W:"), 1, 0)
        processing_form.addWidget(self.window_combo, 1, 1)
        processing_form.addWidget(self.peak_check, 2, 0, 1, 2)
        processing_form.addWidget(QtWidgets.QLabel("Umbral θ:"), 3, 0)
        processing_form.addWidget(self.threshold_spin, 3, 1)
        processing_form.addWidget(self.confirmed_threshold, 4, 0, 1, 2)
        connection_layout.addWidget(processing_group, 1)
        root_layout.addLayout(connection_layout)

        self.device_status = QtWidgets.QLabel("Esperando conexión")
        self.device_status.setObjectName("deviceStatus")
        self.device_status.setStyleSheet(
            "QLabel#deviceStatus { padding: 8px; background: #eef2f5; border-radius: 4px; }"
        )
        root_layout.addWidget(self.device_status)

        values_group = QtWidgets.QGroupBox("Mediciones recibidas")
        values_layout = QtWidgets.QGridLayout(values_group)
        self.temperature_value = self.value_label()
        self.humidity_value = self.value_label()
        self.pressure_value = self.value_label()
        self.average_value = self.value_label()
        self.average_window_status = QtWidgets.QLabel("Sin datos")
        for column, title, value in (
            (0, "Temperatura (°C)", self.temperature_value),
            (1, "Humedad relativa (%HR)", self.humidity_value),
            (2, "Presión (hPa)", self.pressure_value),
            (3, "Promedio ESP32 (°C)", self.average_value),
        ):
            values_layout.addWidget(QtWidgets.QLabel(title), 0, column)
            values_layout.addWidget(value, 1, column)
        values_layout.addWidget(self.average_window_status, 2, 3)
        root_layout.addWidget(values_group)

        charts_group = QtWidgets.QGroupBox("Historial de señales")
        charts_layout = QtWidgets.QHBoxLayout(charts_group)
        self.temperature_plot_host, self.temperature_plot_layout = self.plot_host()
        self.humidity_plot_host, self.humidity_plot_layout = self.plot_host()
        self.pressure_plot_host, self.pressure_plot_layout = self.plot_host()
        charts_layout.addWidget(self.temperature_plot_host)
        charts_layout.addWidget(self.humidity_plot_host)
        charts_layout.addWidget(self.pressure_plot_host)
        root_layout.addWidget(charts_group, 1)

        bottom_layout = QtWidgets.QHBoxLayout()
        peaks_group = QtWidgets.QGroupBox("Historial de eventos de pico")
        peaks_layout = QtWidgets.QVBoxLayout(peaks_group)
        self.peak_events = QtWidgets.QListWidget()
        self.peak_events.setMinimumHeight(140)
        peaks_layout.addWidget(self.peak_events)
        bottom_layout.addWidget(peaks_group, 1)

        confirmation_group = QtWidgets.QGroupBox("Confirmaciones y actividad serial")
        confirmation_layout = QtWidgets.QVBoxLayout(confirmation_group)
        self.confirmed_config = QtWidgets.QLabel("Esperando confirmación del dispositivo")
        self.confirmed_config.setWordWrap(True)
        confirmation_layout.addWidget(self.confirmed_config)
        self.event_log = QtWidgets.QPlainTextEdit()
        self.event_log.setReadOnly(True)
        self.event_log.setMaximumBlockCount(200)
        self.event_log.setMinimumHeight(110)
        confirmation_layout.addWidget(self.event_log)
        bottom_layout.addWidget(confirmation_group, 2)
        root_layout.addLayout(bottom_layout)

        MainWindow.setCentralWidget(self.central_widget)
        QtCore.QMetaObject.connectSlotsByName(MainWindow)

    @staticmethod
    def value_label():
        label = QtWidgets.QLabel("--")
        label.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        label.setStyleSheet("font-size: 22px; font-weight: bold; padding: 4px;")
        return label

    @staticmethod
    def plot_host():
        host = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(host)
        layout.setContentsMargins(2, 2, 2, 2)
        return host, layout
