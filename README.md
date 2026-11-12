# CC5328-Sistemas-embebidos

Repositorio del ramo ordenado por módulos. Ahora incorporando el sensor ambiental **Bosch BME688** con un microcontrolador **ESP32-S3** (ESP-IDF / FreeRTOS) y una interfaz gráfica de control y visualización en tiempo real desarrollada en **Python (PyQt6 + Matplotlib)**.

## Integrantes del Equipo
- **Matías Rojas V.**
- **Samuel Romero M.**

## Video de Demostración
- **Enlace del video de la tarea 2:** https://youtu.be/phNr0ZTDlCw

---

## Estructura principal

- `esp32_firmware/CMakeLists.txt`: configuración de compilación raíz para ESP-IDF.
- `esp32_firmware/main/main.c`: firmware principal ESP32
- `esp32_firmware/main/sensores/bme688.[ch]`: driver modular I2C para el sensor BME688 (modo forzado, autodetección `0x76`/`0x77` y compensación de fábrica en punto flotante para T, H y P sin calentador de gas).
- `gui_python/main.py`: aplicación principal de la interfaz gráfica y receptor de datos, trabajador serial en hilo secundario (`QThread`), decodificador de paquetes y gráficos dinámicos.
- `gui_python/interfaz_v2_ui.py`: definición de la interfaz gráfica para la Tarea 2 (paneles de conexión, adquisición, procesamiento en ESP32, mediciones, gráficos e historial de eventos).
- `gui_python/test.py`: script de prueba rápida por consola para validar comandos y tramas del protocolo serial.
- `gui_python/requirements.txt`: dependencias de librerías para Python (`PyQt6`, `matplotlib`, `pyserial`).
- `DocsProtocolosComunicacionESP32-GUI.md`: especificación técnica detallada del protocolo de comunicación UART.

---

## Conexión Física y Pines (ESP32-S3 + BME688)

La comunicación entre la placa **ESP32-S3** y el módulo **BME688** se realiza mediante el bus **I2C (`I2C_NUM_0`)** a una velocidad de reloj de **400 kHz**, utilizando únicamente 4 cables:

| Pin Sensor BME688 | Pin Placa ESP32-S3 | Función |
| :--- | :--- | :--- |
| **VCC / VIN / 3V3** | **3V3** | Alimentación lógica (3.3 V) |
| **GND** | **GND** | Tierra común |
| **SDA (o SDI)** | **GPIO 15** | Datos seriales I2C (`BME688_SDA_PIN`) |
| **SCL (o SCK)** | **GPIO 16** | Reloj serial I2C (`BME688_SCL_PIN`) |

* **Dirección I2C del esclavo:** El driver implementa autodetección mediante `i2c_master_probe()`, soportando de forma transparente tanto `0x76` (`SDO` a `GND`) como `0x77` (`SDO` flotante o a `3V3`), además de validar el registro `CHIP_ID` (`0xD0 == 0x61`)

---

## Sensor y Procesamiento Embebido en el ESP32-S3

### 1. Adquisición Ambiental (BME688 en Modo Forzado)
- **Variables medidas:** Temperatura ($^\circ\text{C}$), Humedad Relativa ($\%\text{HR}$) y Presión Atmosférica ($\text{hPa}$). El calentador de gas se mantiene deshabilitado (`CTRL_GAS_0 = 0x08`, `CTRL_GAS_1 = 0x00`) para garantizar tiempos de conversión rápidos y sin interferencia térmica interna.
- **Frecuencia de muestreo común ($f_s$):** Configurable dinámicamente desde la GUI sin reiniciar el microcontrolador en **0.5 Hz, 1.0 Hz, 2.0 Hz y 5.0 Hz** (valor inicial: `1.0 Hz`).
- **Selección de variables transmitidas:** Permite activar cualquiera de las **7 combinaciones no vacías** de `{T, H, P}` y rechaza explícitamente la selección vacía (`0,0,0`).

### 2. Procesamiento Local en el Microcontrolador
Todo el procesamiento matemático se ejecuta dentro del ESP32-S3 sobre un buffer circular estático de 300 muestras (`MAX_TEMP_HISTORY = 300`, dimensionado para cubrir hasta $60\text{ s} \times 5\text{ Hz}$):
- **Promedio móvil temporal de temperatura:** Calcula en cada muestra la media aritmética de las lecturas cuyo timestamp $t_i$ cumple $t - W < t_i \le t$, con ventana $W$ configurable en vivo entre **5, 10, 30 y 60 segundos**, informando además si la ventana temporal ya se completó (`win_full=1`) o sigue en formación (`win_full=0`).
- **Detección de máximos locales (picos) de temperatura:** Evalúa sobre las últimas 3 muestras consecutivas la condición $T_{k-1} > T_{k-2}$, $T_{k-1} \ge T_k$ y $T_{k-1} \ge \theta$, con umbral $\theta$ configurable desde la GUI con paso de $0.1\text{ }^\circ\text{C}$. Emite un paquete asincrónico único por cada pico confirmado.
- **Independencia respecto a la transmisión de T cruda:** Si el usuario desmarca la transmisión de Temperatura (`T=0`) pero mantiene activo el promedio móvil o la detección de picos, el ESP32 continúa adquiriendo y procesando la temperatura internamente y envía `temp_avg_c` y los eventos `PEAK` sin incluir `T` cruda en las tramas.

---

## Especificación del Protocolo Serial (`<{DP}>`)

La comunicación se realiza por **UART0 a 115200 baudios** utilizando paquetes de texto ASCII terminados en `\n`. Los mensajes emitidos por el ESP32 comienzan con el marcador `<{DP}>` para separarlos de cualquier log de arranque de ESP-IDF. Documentación completa en `DocsProtocolosComunicacionESP32-GUI.md`.

### Comandos (GUI $\rightarrow$ ESP32)
- `START\n`: Inicia o reanuda la adquisición y transmisión periódica.
- `STOP\n`: Pausa la adquisición y transmisión.
- `SET_ACQ,<fs>,<t>,<h>,<p>\n`: Configura la frecuencia común (`0.5, 1.0, 2.0, 5.0`) y las variables a transmitir (`1` o `0`). Ejemplo: `SET_ACQ,2.0,1,0,1\n`.
- `SET_AVG,<en>,<win_s>\n`: Habilita/deshabilita (`1`/`0`) el promedio móvil y define la ventana $W \in \{5, 10, 30, 60\}$. Ejemplo: `SET_AVG,1,10\n`.
- `SET_PEAK,<en>,<thr_c>\n`: Habilita/deshabilita (`1`/`0`) la detección de picos y fija el umbral $\theta$. Ejemplo: `SET_PEAK,1,28.0\n`.

### Mensajes y Datos (ESP32 $\rightarrow$ GUI)
- **Muestra periódica (`SAMPLE`):**
  `<{DP}>SAMPLE,seq=14,t_ms=14050,T=23.53,H=46.63,P=927.88,temp_avg_c=23.51,win_full=1`
- **Evento de pico (`PEAK`):**
  `<{DP}>PEAK,t_ms=15500,peak_c=28.70,thr_c=28.0`
- **Confirmación de comando (`ACK`):**
  `<{DP}>ACK,cmd=SET_ACQ,fs=2.0,T=1,H=0,P=1,avg_en=1,win_s=10,peak_en=1,thr_c=28.0`
- **Rechazo o fallo (`ERR`):**
  `<{DP}>ERR,cmd=SET_ACQ,msg=seleccion_vacia` (o `frecuencia_invalida`, `ventana_invalida`, `ESP_ERR_TIMEOUT`, etc.).

---

## Interfaz Gráfica (GUI)
Aplicación desarrollada en Python 3.8+ utilizando PyQt6 y Matplotlib.
- `gui_python/main.py` implementa la interfaz de la Tarea 2 y la comunicación serial asincrónica mediante `SerialWorker` y `QThread`.
- El selector de puerto detecta automáticamente los puertos del sistema operativo y filtra inicialmente adaptadores USB-Serial comunes (ESP32, CP210x, CH340 y FTDI), permitiendo desactivar el filtro para mostrar todos los puertos.
- Permite seleccionar puerto y velocidad, frecuencia de muestreo común (`0.5, 1.0, 2.0, 5.0 Hz`) y las variables `T/H/P` que se transmiten.
- Configura desde la GUI el promedio móvil ($W \in \{5, 10, 30, 60\}\text{ s}$) y la detección de máximos locales ($\theta$ con paso de $0.1\text{ }^\circ\text{C}$) ejecutados por el ESP32.
- Muestra valores actuales, estado de la ventana de promedio (`Ventana en formación` / `Ventana completa`), 3 gráficos históricos independientes, lista de eventos `PEAK` y consola de confirmaciones `ACK` y errores `ERR`.

---

## Decisiones de Diseño

1. **Compensación térmica interna continua (`osrs_t`):** Según las ecuaciones oficiales de Bosch para el BME688, el cálculo compensado de Presión y Humedad depende directamente de la variable térmica `t_fine` (`last_t_fine` y `last_temp_comp`). Por ello, aunque el usuario desmarque `Temperatura (T)` y solo solicite `H` o `P`, el driver mantiene activo `osrs_t = 0x01` en el registro `CTRL_MEAS` para no degradar la exactitud barométrica/higrométrica, pero el firmware omite el campo `,T=` en la trama serial enviada a la GUI.
2. **Reconexión automática en caliente del sensor I2C:** Si ocurre una desconexión temporal de un cable I2C durante la lectura, `bme688_read()` marca `sensor_initialized = false`, reporta el error `<{DP}>ERR,cmd=SENSOR,msg=ESP_ERR_TIMEOUT` sin enviar datos inventados y re-ejecuta automáticamente `bme688_init()` apenas el sensor vuelve a estar disponible.
3. **Compensación del tiempo de conversión I2C:** Para respetar con precisión las frecuencias de `0.5, 1.0, 2.0 y 5.0 Hz`, la tarea `task_adquisicion_bme` mide el tiempo consumido por la transacción I2C (`elapsed_ms`) y lo descuenta del período objetivo (`period_ms - elapsed_ms`).

---

## Resumen de Pruebas Realizadas

| # | Prueba Obligatoria | Procedimiento | Resultado Obtenido |
| :--- | :--- | :--- | :--- |
| **1** | **Lecturas reales (T, H, P)** | Conexión en `COM3` a `115200` baudios, `fs = 1.0 Hz` con `T=1, H=1, P=1`. | Lecturas estables en tiempo real (`T ≈ 23.5–27.0 °C`, `H ≈ 46.5–60.4 %HR`, `P ≈ 927.8–933.1 hPa`). |
| **2** | **7 combinaciones de T, H, P** | Activación selectiva desde la GUI (`T`, `H`, `P`, `T+H`, `T+P`, `H+P`, `T+H+P`) y prueba `0,0,0`. | El ESP32 transmite solo las claves activas, la GUI limpia las curvas deshabilitadas y se rechaza la selección vacía. |
| **3** | **Cambio dinámico de $f_s$** | Alternancia en vivo entre `0.5, 1.0, 2.0 y 5.0 Hz` mediante `SET_ACQ`. | Confirmación por `ACK` inmediata y actualización del intervalo entre timestamps `t_ms` (2000, 1000, 500 y 200 ms) sin reiniciar. |
| **4** | **Promedio móvil ($W$)** | Activación de `SET_AVG` cambiando $W$ entre `5, 10, 30 y 60 s`. | Transición correcta de `Ventana en formación` (`win_full=0`) a `Ventana completa` (`win_full=1`) y suavizado inmediato al cambiar $W$. |
| **5** | **Detección de picos ($\theta$)** | Configuración de umbral $\theta = 28.0\text{ }^\circ\text{C}$ y perturbación térmica soplando el sensor. | Al superar $28.0\text{ }^\circ\text{C}$ e iniciar el descenso, el ESP32 emite un único paquete `PEAK` registrado en la lista de eventos de la GUI. |
| **6** | **Promedio y picos con `T=0`** | Desmarcar `Temperatura (T)` manteniendo `P=1`, promedio activo y detección de picos activa. | La trama `SAMPLE` omite `T=`, pero `Promedio ESP32 (°C)` y los eventos `PEAK` continúan actualizándose en vivo. |
| **7** | **Manejo de errores y fallos** | Envío de parámetros fuera de rango y desconexión temporal del sensor/puerto. | Rechazo mediante paquete `ERR`, aviso de `"Sin muestras por más de 3 s"` en la GUI y recuperación limpia al reconectar. |

---

## Instrucciones de Compilación y Ejecución

### **1. Firmware ESP32-S3 (ESP-IDF v5.5+):**
Desde la raíz del proyecto, ingresar a la carpeta del microcontrolador, fijar el target `esp32s3`, compilar y flashear:
```bash
cd esp32_firmware
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash
#(Reemplazar COM3 por el puerto correspondiente en Windows o /dev/ttyACM0 / /dev/ttyUSB0 en Linux).
```

### **2. Aplicación Python:**
Desde la raíz del proyecto, instalar las dependencias en un entorno virtual y ejecutar la interfaz:

```bash
# Crear el entorno virtual
python3 -m venv gui_python/.venv

# Activarlo en Linux/macOS
source gui_python/.venv/bin/activate

# Activarlo en Windows (PowerShell)
gui_python\.venv\Scripts\Activate.ps1

# Instalar dependencias y ejecutar la GUI
pip install -r gui_python/requirements.txt
python gui_python/main.py
```

## Nota

La carpeta build/ contiene archivos generados por compilación y no debe editarse manualmente.
