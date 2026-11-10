# CC5328-Sistemas-embebidos

Repositorio del ramo ordenado por módulos.

## Estructura principal

- `esp32_firmware/main/main.c`: firmware principal ESP32
- `esp32_firmware/main/sensores/acelerometro.[ch]`: sensor tri-axial sintético
- `esp32_firmware/main/sensores/ambiente.[ch]`: temperatura y humedad sintéticas
- `esp32_firmware/main/commUART.C`: comunicación UART
- `esp32_firmware/main/ejemplos_uso.c`: ejemplos de configuración y uso
- `gui_python/main.py`: aplicación principal de la interfaz gráfica y receptor de datos
- `gui_python/interfaz_v1_ui.py`: diseño visual exportado de Qt Designer
- `gui_python/requirements.txt`: dependencias de librerías para Python
- `DocsSensores.md`: documentación técnica

## Sensores implementados

### Acelerómetro tri-axial
- Función armónica simple
- Función modulada en amplitud
- Función multicomponente
- Configuración independiente por eje X, Y, Z

### Variables ambientales
- Temperatura aleatoria entre 15.0 y 30.0 °C
- Humedad relativa aleatoria entre 20 y 40 %
- Intervalo de envío configurable a 30 o 60 segundos

## Interfaz Gráfica (GUI)
Aplicación desarrollada en Python 3.8+ utilizando PyQt6 y Matplotlib.
- `gui_python/main.py` implementa la interfaz T2 y la comunicación serial en un hilo de trabajo.
- El selector de puerto filtra inicialmente adaptadores USB-serial comunes (ESP32, CP210x, CH340 y FTDI); se puede desactivar para mostrar todos los puertos.
- Permite seleccionar puerto y velocidad, frecuencia de muestreo y las variables T/H/P que se transmiten.
- Configura desde la GUI el promedio móvil y la detección de máximos locales ejecutados por el ESP32.
- Muestra valores, gráficos históricos, promedio confirmado/recibido, eventos de pico, ACK y errores.
- El protocolo serial esperado está documentado en `DocsProtocolosComunicacionESP32-GUI.md`.

## Instrucciones de Compilación y Ejecución

**1. Firmware ESP32:**
Desde la raíz del proyecto, ingresar a la carpeta del microcontrolador y compilar utilizando ESP-IDF

**2. Aplicación Python:**
Desde la raíz del proyecto, instalar las dependencias en un entorno virtual y ejecutar la interfaz:

```bash
# Crear el entorno virtual
python3 -m venv gui_python/.venv

# Activarlo en Linux/macOS
source gui_python/.venv/bin/activate

# Activarlo en Windows (PowerShell)
# gui_python\.venv\Scripts\Activate.ps1

# Instalar dependencias y ejecutar la GUI
pip install -r gui_python/requirements.txt
python gui_python/main.py
```

La GUI utiliza el marcador `<{DP}>` y los mensajes `SAMPLE`, `PEAK`, `ACK` y `ERR` descritos en el documento de protocolo. Las frecuencias, ventanas y comandos enviados corresponden a los valores definidos por la T2.

## Nota

La carpeta build/ contiene archivos generados por compilación y no debe editarse manualmente.
