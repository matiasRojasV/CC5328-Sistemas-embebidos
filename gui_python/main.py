import sys
import threading
import time
import serial
import struct
from collections import deque
from PyQt6.QtWidgets import QApplication, QMainWindow, QVBoxLayout, QWidget
from PyQt6.QtCore import QObject, QThread, QTimer, pyqtSignal, pyqtSlot
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure

# importamos la cascara hecha en pyside6 + pyuic6
from interfaz_v1_ui import Ui_MainWindow

class LivePlot(FigureCanvas):
    def __init__(self, max_points: int = 100, title= "Grafico", parent=None, fps = 30):

        fig = Figure(figsize=(3, 2), dpi=100)
        self.axes = fig.add_subplot(111)

        fig.tight_layout(pad=1.5) # para qe nose corten los titulos y etiquetas del grafico

        super().__init__(fig)

        self.setParent(parent)
        self.axes.set_title(title, fontsize=9)
        self.axes.tick_params(axis='both', labelsize=8)

        # estructura de deque para que se vayan borrando los datos mas viejos
        self.max_points = max_points
        self.x_data = deque(maxlen=max_points)
        self.y_data = deque(maxlen=max_points)

        # inicializamos el grafico
        self.line, = self.axes.plot([], [], 'r-')

        #aplicamos la tasa de refresco del grafico
        self.drawing_timer = QTimer(self)
        self.drawing_timer.timeout.connect(self.update_canvas)
        self.drawing_timer.start(fps)

    def add_point(self, x, y):
        self.x_data.append(x)
        self.y_data.append(y)

        # actualizamos los datos de la linea

        self.line.set_data(self.x_data, self.y_data)

        # ajustamos los limites del grafico de forma dinamica
        self.axes.set_xlim(max(0, x - self.max_points), x+5)
        self.axes.set_ylim(min(self.y_data) - 1, max(self.y_data) + 1 if self.y_data else 5)

    def update_canvas(self):

        #usamos la funcion draw_idle para que no se bloquee la interfaz (es una funncion de matplotlib para redibujar mas facil)
        self.draw_idle()

class DataReceiver(QObject):

    #definimo una señal por cada posible dato para avisarle a la GUI que llegaron datos
    accel_received = pyqtSignal(float, float, float)  # Señal para aceleración (x, y, z)
    env_received = pyqtSignal(float, float)  # Señal para temperatura y humedad
    error_received = pyqtSignal(str)  # Señal para errores de lectura

    def __init__(self, port, baudrate=115200):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.running = True
        self.serial_conn = None

    @pyqtSlot()
    def receiver_loop(self):
        MARKER = b"<{DP}>"
        HEADER_LEN = len(MARKER) + 2

        try:
            #abrimos el puerto serial 
            self.serial_conn = serial.Serial(self.port, self.baudrate)
            buf = bytearray()

            # leemos los datos del puerto serial en el while
            while self.running:
                chunk = self.serial_conn.read(4096)
                if not chunk:
                    continue
                buf += chunk

                while True: #desempaquetamos la info que viene
                    start = buf.find(MARKER)
                    if start < 0:
                        cut = max(0, len(buf) - len(MARKER))
                        del buf[:cut]
                        break
                    if start > 0:
                        del buf[:start]
                    if len(buf) < HEADER_LEN:
                        break

                    float_num, = struct.unpack_from("<H", buf, len(MARKER))
                    data_len = float_num * 4
                    if len(buf) < HEADER_LEN + data_len:
                        break

                    data = buf[:HEADER_LEN + data_len]
                    values = struct.unpack(f"<{float_num}f", data)

                    # enviamos las señales de los datos correspondientes a los graficos segun la cantidad de valores recibidos
                    if float_num == 3: # si se reciben 3 valores son datos del acelerometro
                        self.accel_received.emit(values[0], values[1], values[2])
                    elif float_num == 2: # si son 2 valores son datos de temperatura y humedad
                        self.env_received.emit(values[0], values[1])
        except Exception as e:
            self.error_received.emit(str(e))
        finally:
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.close()

    def stop(self):
        self.running = False

class AppWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        # inicializamos la interfaz hecha con qtdesigner
        self.ui = Ui_MainWindow()
        self.ui.setupUi(self)

        # agregamos puertos seriales de prueba para windows
        self.ui.comboBox_puerto.addItems(["COM3", "COM4"])
        # Agregamos puertos seriales de prueba para Linux
        # self.ui.comboBox_puerto.addItems(["/dev/ttyUSB0"]) 
        
        #creamos las instancias de los graficos dinamicos del acelerometro
        self.plot_x = LivePlot(title="Aceleracion Eje X")
        self.plot_y = LivePlot(title="Aceleracion Eje Y")
        self.plot_z = LivePlot(title="Aceleracion Eje Z")
        self.plot_ambiente = LivePlot(title="Historial Temperatura/Humedad")

        # incrustamos las instacias recien creadas en los layouts vacios de la interfaz
        self.ui.layout_x.addWidget(self.plot_x)
        self.ui.layout_y.addWidget(self.plot_y)
        self.ui.layout_z.addWidget(self.plot_z)
        self.ui.layout_ambiente.addWidget(self.plot_ambiente)

        self.thread = QThread()
        self.receiver = None
        self.init_time = time.time()

        self.ui.btn_conectar.clicked.connect(self.start_connection)
        self.ui.btn_desconectar.clicked.connect(self.stop_connection)

    @pyqtSlot(float, float, float)
    def update_accel(self, x, y, z):
        # tiempo desde la conexion
        current_time = time.time() - self.init_time

        # enviar datos
        self.plot_x.add_point(current_time, x)
        self.plot_y.add_point(current_time, y)
        self.plot_z.add_point(current_time, z)

    @pyqtSlot(float, float)
    def update_env(self, temp, hum):
        # tiempo desde la conexion
        current_time = time.time() - self.init_time

        self.plot_env.add_point(current_time, temp) #graficamos la temperatura

        #actualizamos las cajas LCD de la interfaz
        self.ui.lcdNumber_temp.display(temp)
        self.ui.lcdNumber_hum.display(hum)

    def start_connection(self):

        # si ya hay un hilo de conexion corriendo, no hacemos nada
        if self.thread.isRunning():
            return

        #capturamos los datos de la conexion de la GUI
        puerto = self.ui.comboBox_puerto.currentText()
        baudrate = int(self.ui.comboBox_baudrate.currentText())

        #reiniciamos el tiempo de los graficos
        self.init_time = time.time()

        # creamos el receptor y lo movemos a un hilo aparte (secundario)
        self.receiver = DataReceiver(port= puerto, baudrate=baudrate)
        self.receiver.moveToThread(self.thread)

        #conectamos la señales para iniciar el loop y comenzar a recibir los datos
        self.thread.started.connect(self.receiver.receiver_loop)
        self.receiver.accel_received.connect(self.update_accel)
        self.receiver.env_received.connect(self.update_env)

        # iniciamos el hilo
        self.thread.start()

        print(f"conectando al puerto {puerto} a {baudrate} baudios")

    def stop_connection(self):
        if self.receiver:
            self.receiver.stop() # se rompe el ciclo del while true receptor
        if self.thread.isRunning():
            self.thread.quit() # se cierra el hilo (se lo pide al hilo de Qt)
            self.thread.wait() # esperamos a que termine de cerrarse
            print("conexion detenida")


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = AppWindow()
    window.show()
    sys.exit(app.exec())