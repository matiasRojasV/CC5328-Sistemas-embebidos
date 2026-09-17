import sys
import threading
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

class AppWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        # inicializamos la interfaz hecha con qtdesigner
        self.ui = Ui_MainWindow()
        self.ui.setupUi(self)

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

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = AppWindow()
    window.show()
    sys.exit(app.exec())