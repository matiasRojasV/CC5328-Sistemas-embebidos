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
- Conexión serial UART asincrónica mediante QThread.
- Visualización en tiempo real con ventanas deslizantes para 3 ejes de aceleración.
- Gráfico histórico y displays digitales para variables ambientales.
- Panel de control dinámico para alterar parámetros de muestreo en el microcontrolador.

## Instrucciones de Compilación y Ejecución

**1. Firmware ESP32:**
Desde la raíz del proyecto, ingresar a la carpeta del microcontrolador y compilar utilizando ESP-IDF:
```bash
rellenar
```
**2. Aplicación Python:**
desde la raiz del proyecto, instalar las dependencias en un entorno virtual y ejecutar la interfaz

```bash
# Para crear el Ambiente virtual:

## en windows
python -m venv .venv
## en linux (creo)
python3 -m venv .venv

# Instalacion de dependencias
pip install -r gui_python/requirements.txt

# Ejecucion de la interfaz
python gui_python/main.py
```

## Nota

La carpeta build/ contiene archivos generados por compilación y no debe editarse manualmente.
