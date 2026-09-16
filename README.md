# CC5328-Sistemas-embebidos

Repositorio del ramo ordenado por módulos.

## Estructura principal

- main/main.c: firmware principal ESP32
- main/sensores/acelerometro.[ch]: sensor tri-axial sintético
- main/sensores/ambiente.[ch]: temperatura y humedad sintéticas
- main/commUART.C: comunicación UART
- main/ejemplos_uso.c: ejemplos de configuración y uso
- main/DOCUMENTACION_SENSORES.md: documentación técnica

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

## Nota

La carpeta build/ contiene archivos generados por compilación y no debe editarse manualmente.
