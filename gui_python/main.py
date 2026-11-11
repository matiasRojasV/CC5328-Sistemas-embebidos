import math
import queue
import sys
import time
from collections import deque

import serial
from serial.tools import list_ports
from PyQt6.QtCore import QObject, QThread, QTimer, Qt, pyqtSignal
from PyQt6.QtWidgets import QApplication, QMainWindow, QMessageBox
from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure

from interfaz_v2_ui import Ui_MainWindow


MARKER = "<{DP}>"
MAX_PLOT_POINTS = 300
USB_SERIAL_VIDS = {0x0403, 0x10C4, 0x1A86, 0x303A}
USB_SERIAL_KEYWORDS = (
    "esp32",
    "cp210",
    "ch340",
    "ch910",
    "ftdi",
    "silicon labs",
    "usb serial",
    "usb jtag",
    "usb-uart",
    "usb uart",
)


def parse_packet(line):
    """Parse one protocol line into its message type and key/value fields."""
    marker_index = line.find(MARKER)
    if marker_index < 0:
        return None, {}

    parts = line[marker_index + len(MARKER):].strip().split(",")
    if not parts or not parts[0]:
        return None, {}

    fields = {}
    for item in parts[1:]:
        if "=" in item:
            key, value = item.split("=", 1)
            fields[key.strip()] = value.strip()
    return parts[0].strip(), fields


class SerialWorker(QObject):
    connected = pyqtSignal(str)
    disconnected = pyqtSignal()
    packet_received = pyqtSignal(str, dict)
    error_received = pyqtSignal(str)
    finished = pyqtSignal()

    def __init__(self, port, baudrate):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.commands = queue.Queue()
        self.stop_requested = False

    def send_command(self, command):
        self.commands.put(command)

    def request_stop(self):
        self.stop_requested = True

    def run(self):
        connection = None
        try:
            connection = serial.Serial(
                self.port,
                baudrate=self.baudrate,
                timeout=0.1,
                write_timeout=1,
            )
            self.connected.emit(f"Conectado a {self.port} ({self.baudrate} baudios)")
            buffer = bytearray()

            while not self.stop_requested:
                while True:
                    try:
                        command = self.commands.get_nowait()
                    except queue.Empty:
                        break
                    connection.write(command.encode("ascii"))

                chunk = connection.read(1024)
                if not chunk:
                    continue
                buffer.extend(chunk)

                while b"\n" in buffer:
                    raw_line, _, remainder = buffer.partition(b"\n")
                    buffer = bytearray(remainder)
                    line = raw_line.decode("utf-8", errors="replace").strip()
                    message_type, fields = parse_packet(line)
                    if message_type is not None:
                        self.packet_received.emit(message_type, fields)

            connection.write(b"STOP\n")
        except (serial.SerialException, OSError, UnicodeError) as error:
            self.error_received.emit(f"Error serial: {error}")
        finally:
            try:
                if connection is not None and connection.is_open:
                    connection.close()
            except (serial.SerialException, OSError) as error:
                self.error_received.emit(f"Error al cerrar el puerto serial: {error}")
            finally:
                self.disconnected.emit()
                self.finished.emit()


class HistoryPlot(FigureCanvas):
    def __init__(self, title, unit, color, parent=None):
        figure = Figure(figsize=(4, 2.4), dpi=100)
        self.axes = figure.add_subplot(111)
        super().__init__(figure)
        self.setParent(parent)
        self.axes.set_title(title)
        self.axes.set_xlabel("Tiempo desde inicio (s)")
        self.axes.set_ylabel(unit)
        self.axes.grid(True, alpha=0.3)
        figure.tight_layout(pad=1.0)
        self.line, = self.axes.plot([], [], color=color)
        self.times = deque(maxlen=MAX_PLOT_POINTS)
        self.values = deque(maxlen=MAX_PLOT_POINTS)

    def add_point(self, timestamp, value):
        self.times.append(timestamp)
        self.values.append(value)
        self.line.set_data(self.times, self.values)
        self.axes.relim()
        self.axes.autoscale_view()
        self.draw_idle()

    def clear(self):
        self.times.clear()
        self.values.clear()
        self.line.set_data([], [])
        self.draw_idle()


class AppWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.ui = Ui_MainWindow()
        self.ui.setupUi(self)
        self.worker = None
        self.serial_thread = None
        self.last_sample_time = None
        self.last_sample_timestamp = 0.0
        self.sample_count = 0

        self.plot_temperature = HistoryPlot("Temperatura", "°C", "#c0392b")
        self.plot_humidity = HistoryPlot("Humedad relativa", "%HR", "#2471a3")
        self.plot_pressure = HistoryPlot("Presión", "hPa", "#148f77")
        self.ui.temperature_plot_layout.addWidget(self.plot_temperature)
        self.ui.humidity_plot_layout.addWidget(self.plot_humidity)
        self.ui.pressure_plot_layout.addWidget(self.plot_pressure)

        self.ui.window_combo.addItems(["5", "10", "30", "60"])
        self.ui.window_combo.setCurrentText("10")
        self.ui.frequency_combo.addItems(["0.5", "1.0", "2.0", "5.0"])
        self.ui.frequency_combo.setCurrentText("1.0")
        self.ui.baudrate_combo.addItems(["9600", "57600", "115200", "230400"])
        self.ui.baudrate_combo.setCurrentText("115200")
        self.ui.threshold_spin.setRange(-40.0, 85.0)
        self.ui.threshold_spin.setDecimals(1)
        self.ui.threshold_spin.setSingleStep(0.1)
        self.ui.threshold_spin.setValue(28.0)

        self.ui.refresh_ports_button.clicked.connect(self.refresh_ports)
        self.ui.usb_filter_check.toggled.connect(self.refresh_ports)
        self.ui.connect_button.clicked.connect(self.start_connection)
        self.ui.disconnect_button.clicked.connect(self.stop_connection)
        self.ui.apply_button.clicked.connect(self.apply_configuration)
        self.ui.start_button.clicked.connect(lambda: self.send_command("START\n"))
        self.ui.pause_button.clicked.connect(lambda: self.send_command("STOP\n"))
        self.ui.temperature_check.toggled.connect(self.update_transmission_indicators)
        self.ui.humidity_check.toggled.connect(self.update_transmission_indicators)
        self.ui.pressure_check.toggled.connect(self.update_transmission_indicators)

        self.refresh_ports()
        self.update_transmission_indicators()
        self.set_connected_state(False)

        self.no_data_timer = QTimer(self)
        self.no_data_timer.timeout.connect(self.check_data_timeout)
        self.no_data_timer.start(1000)

    def refresh_ports(self):
        selected_port = self.ui.port_combo.currentData() or self.ui.port_combo.currentText()
        self.ui.port_combo.clear()
        ports = list(list_ports.comports())
        if self.ui.usb_filter_check.isChecked():
            ports = [port for port in ports if self.is_likely_esp32_port(port)]
        for port in ports:
            description = " ".join(
                item for item in (port.description, port.manufacturer, port.hwid) if item
            )
            self.ui.port_combo.addItem(port.device, userData=port.device)
            self.ui.port_combo.setItemData(
                self.ui.port_combo.count() - 1,
                description,
                Qt.ItemDataRole.ToolTipRole,
            )
        if selected_port:
            index = self.ui.port_combo.findData(selected_port)
            if index >= 0:
                self.ui.port_combo.setCurrentIndex(index)
        if not ports:
            if self.ui.usb_filter_check.isChecked():
                self.ui.connection_status.setText(
                    "No se encontraron adaptadores USB-serial comunes; desactiva el filtro para ver todos."
                )
            else:
                self.ui.connection_status.setText("No se encontraron puertos seriales")

    @staticmethod
    def is_likely_esp32_port(port):
        if port.vid in USB_SERIAL_VIDS:
            return True
        details = " ".join(
            item for item in (port.description, port.manufacturer, port.hwid) if item
        ).lower()
        return any(keyword in details for keyword in USB_SERIAL_KEYWORDS)

    def start_connection(self):
        if self.serial_thread is not None:
            return
        port = self.ui.port_combo.currentData() or self.ui.port_combo.currentText()
        if not port:
            QMessageBox.warning(self, "Puerto requerido", "Selecciona un puerto serial disponible.")
            return
        try:
            baudrate = int(self.ui.baudrate_combo.currentText())
        except ValueError:
            QMessageBox.warning(self, "Velocidad inválida", "Selecciona una velocidad serial válida.")
            return

        self.last_sample_time = None
        self.last_sample_timestamp = 0.0
        self.sample_count = 0
        self.clear_measurements()
        self.update_transmission_indicators()
        self.serial_thread = QThread(self)
        self.worker = SerialWorker(port, baudrate)
        self.worker.moveToThread(self.serial_thread)
        self.serial_thread.started.connect(self.worker.run)
        self.worker.connected.connect(self.on_connected)
        self.worker.disconnected.connect(self.on_disconnected)
        self.worker.packet_received.connect(self.handle_packet)
        self.worker.error_received.connect(self.show_error)
        self.worker.finished.connect(self.serial_thread.quit)
        self.worker.finished.connect(self.worker.deleteLater)
        self.serial_thread.finished.connect(self.on_thread_finished)
        self.serial_thread.start()
        self.ui.connection_status.setText(f"Conectando a {port}...")
        self.set_connected_state(False, connecting=True)

    def stop_connection(self):
        if self.worker is not None:
            self.worker.request_stop()
            self.ui.connection_status.setText("Desconectando...")
            self.set_connected_state(False, connecting=True)

    def on_connected(self, message):
        self.ui.connection_status.setText(message)
        self.ui.device_status.setText("Dispositivo conectado; esperando datos")
        self.last_sample_time = time.monotonic()
        self.set_connected_state(True)

    def on_disconnected(self):
        self.set_connected_state(False)
        self.ui.device_status.setText("Sin conexión")
        if self.worker is not None and self.worker.stop_requested:
            self.ui.connection_status.setText("Desconectado")
        else:
            self.ui.connection_status.setText("Conexión cerrada")

    def on_thread_finished(self):
        self.worker = None
        self.serial_thread = None
        self.set_connected_state(False)

    def set_connected_state(self, connected, connecting=False):
        self.ui.connect_button.setEnabled(not connected and not connecting)
        self.ui.disconnect_button.setEnabled(connected or connecting)
        self.ui.apply_button.setEnabled(connected)
        self.ui.start_button.setEnabled(connected)
        self.ui.pause_button.setEnabled(connected)
        self.ui.port_combo.setEnabled(not connected and not connecting)
        self.ui.usb_filter_check.setEnabled(not connected and not connecting)
        self.ui.baudrate_combo.setEnabled(not connected and not connecting)
        self.ui.refresh_ports_button.setEnabled(not connected and not connecting)

    def send_command(self, command):
        if self.worker is None or self.serial_thread is None:
            self.show_error("No hay una conexión serial activa.")
            return
        self.worker.send_command(command)
        self.add_log(f"TX  {command.strip()}")

    def apply_configuration(self):
        variables = [
            ("T", self.ui.temperature_check),
            ("H", self.ui.humidity_check),
            ("P", self.ui.pressure_check),
        ]
        if not any(control.isChecked() for _, control in variables):
            QMessageBox.warning(
                self,
                "Selección vacía",
                "Selecciona al menos una variable para transmitir (T, H o P).",
            )
            return

        selected = {name: int(control.isChecked()) for name, control in variables}
        frequency = self.ui.frequency_combo.currentText()
        avg_enabled = int(self.ui.average_check.isChecked())
        window_seconds = self.ui.window_combo.currentText()
        peak_enabled = int(self.ui.peak_check.isChecked())
        threshold = f"{self.ui.threshold_spin.value():.1f}"

        self.send_command(
            f"SET_ACQ,{frequency},{selected['T']},{selected['H']},{selected['P']}\n"
        )
        self.send_command(f"SET_AVG,{avg_enabled},{window_seconds}\n")
        self.send_command(f"SET_PEAK,{peak_enabled},{threshold}\n")
        self.update_transmission_indicators()

    def update_transmission_indicators(self, *_):
        for control, indicator, plot in (
            (self.ui.temperature_check, self.ui.temperature_transmission, self.plot_temperature),
            (self.ui.humidity_check, self.ui.humidity_transmission, self.plot_humidity),
            (self.ui.pressure_check, self.ui.pressure_transmission, self.plot_pressure),
        ):
            enabled = control.isChecked()
            indicator.setText("Transmitida" if enabled else "No transmitida")
            plot.setEnabled(enabled)
            if not enabled:
                plot.clear()

    def clear_measurements(self):
        self.ui.temperature_value.setText("--")
        self.ui.humidity_value.setText("--")
        self.ui.pressure_value.setText("--")
        self.ui.average_value.setText("--")
        self.ui.average_window_status.setText("Sin datos")
        self.ui.temperature_transmission.setText("Sin datos")
        self.ui.humidity_transmission.setText("Sin datos")
        self.ui.pressure_transmission.setText("Sin datos")
        self.plot_temperature.clear()
        self.plot_humidity.clear()
        self.plot_pressure.clear()
        self.ui.peak_events.clear()
        self.ui.confirmed_config.setText("Esperando confirmación del dispositivo")

    def handle_packet(self, message_type, fields):
        if message_type == "SAMPLE":
            self.handle_sample(fields)
        elif message_type == "PEAK":
            self.handle_peak(fields)
        elif message_type == "ACK":
            self.handle_ack(fields)
        elif message_type in ("ERR", "ERROR"):
            self.show_error(
                f"Comando rechazado ({fields.get('cmd', 'desconocido')}): "
                f"{fields.get('msg', 'sin detalle')}"
            )
        else:
            self.add_log(f"RX  Mensaje desconocido: {message_type}")

    @staticmethod
    def parse_number(fields, key):
        value = fields.get(key)
        if value is None:
            return None
        try:
            number = float(value)
        except ValueError:
            return None
        return number if math.isfinite(number) else None

    def handle_sample(self, fields):
        timestamp = self.parse_number(fields, "t_ms")
        if timestamp is None:
            timestamp = self.last_sample_timestamp + 1.0
        timestamp /= 1000.0
        if timestamp < self.last_sample_timestamp:
            self.plot_temperature.clear()
            self.plot_humidity.clear()
            self.plot_pressure.clear()
        self.last_sample_timestamp = timestamp
        self.last_sample_time = time.monotonic()
        self.sample_count += 1

        sample_keys = (
            ("T", self.ui.temperature_check, self.ui.temperature_value, self.plot_temperature),
            ("H", self.ui.humidity_check, self.ui.humidity_value, self.plot_humidity),
            ("P", self.ui.pressure_check, self.ui.pressure_value, self.plot_pressure),
        )
        for key, control, value_label, plot in sample_keys:
            value = self.parse_number(fields, key)
            if not control.isChecked():
                value_label.setText("--")
                if self.sample_count == 1:
                    plot.clear()
                continue
            if value is None:
                value_label.setText("Inválida" if key in fields else "Sin dato")
                plot.add_point(timestamp, math.nan)
            else:
                value_label.setText(f"{value:.2f}")
                plot.add_point(timestamp, value)

        average = self.parse_number(fields, "temp_avg_c")
        if self.ui.average_check.isChecked():
            if average is not None:
                self.ui.average_value.setText(f"{average:.2f} °C")
            elif "temp_avg_c" in fields:
                self.ui.average_value.setText("Inválido")
            else:
                self.ui.average_value.setText("Sin dato")
            complete = fields.get("win_full")
            if complete == "1":
                self.ui.average_window_status.setText("Ventana completa")
            elif complete == "0":
                self.ui.average_window_status.setText("Ventana en formación")
            elif "temp_avg_c" not in fields:
                self.ui.average_window_status.setText("Sin resultado en paquete")
            else:
                self.ui.average_window_status.setText("Estado no informado")
        else:
            self.ui.average_value.setText("Deshabilitado")
            self.ui.average_window_status.setText("Deshabilitado")

        sequence = fields.get("seq", "?")
        self.ui.device_status.setText(f"Recibiendo datos · muestra #{sequence}")

    def handle_peak(self, fields):
        peak = self.parse_number(fields, "peak_c")
        threshold = self.parse_number(fields, "thr_c")
        timestamp = self.parse_number(fields, "t_ms")
        if peak is None or threshold is None:
            self.show_error("Evento PEAK inválido: falta peak_c o thr_c.")
            return
        time_text = "tiempo desconocido" if timestamp is None else f"{timestamp / 1000.0:.2f} s"
        self.ui.peak_events.insertItem(
            0,
            f"{time_text} · máximo {peak:.2f} °C · umbral {threshold:.2f} °C",
        )
        while self.ui.peak_events.count() > 100:
            self.ui.peak_events.takeItem(self.ui.peak_events.count() - 1)
        self.ui.confirmed_threshold.setText(f"Umbral confirmado: {threshold:.1f} °C")
        self.add_log(f"EVENTO PEAK  {time_text}: {peak:.2f} °C (umbral {threshold:.2f} °C)")

    def handle_ack(self, fields):
        command = fields.get("cmd", "comando")
        details = ", ".join(
            f"{key}={value}"
            for key, value in fields.items()
            if key != "cmd"
        )
        self.ui.confirmed_config.setText(
            f"Confirmado por ESP32 · {command}" + (f" · {details}" if details else "")
        )
        if "thr_c" in fields:
            self.ui.confirmed_threshold.setText(f"Umbral confirmado: {fields['thr_c']} °C")
        self.add_log(f"ACK  {command}" + (f" · {details}" if details else ""))

    def check_data_timeout(self):
        if (
            self.serial_thread is not None
            and self.serial_thread.isRunning()
            and self.last_sample_time is not None
            and time.monotonic() - self.last_sample_time > 3.0
        ):
            self.ui.device_status.setText("Sin muestras por más de 3 s")

    def show_error(self, message):
        self.ui.device_status.setText(message)
        self.add_log(f"ERROR  {message}")

    def add_log(self, message):
        self.ui.event_log.appendPlainText(message)
        maximum_blocks = 200
        document = self.ui.event_log.document()
        while document.blockCount() > maximum_blocks:
            cursor = self.ui.event_log.textCursor()
            cursor.movePosition(cursor.MoveOperation.Start)
            cursor.select(cursor.SelectionType.BlockUnderCursor)
            cursor.removeSelectedText()
            cursor.deleteChar()

    def closeEvent(self, event):
        if self.worker is not None:
            self.worker.request_stop()
            if self.serial_thread is not None and not self.serial_thread.wait(1500):
                QMessageBox.warning(
                    self,
                    "Desconexión pendiente",
                    "El puerto serial aún está cerrándose. Intenta cerrar la ventana nuevamente.",
                )
                event.ignore()
                return
        event.accept()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = AppWindow()
    window.show()
    sys.exit(app.exec())
