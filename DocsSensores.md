# Documentación: Sensores del Proyecto ESP32

## 1. Acelerómetro Tri-axial

### Archivos
- `acelerometro.h` - Header con estructuras
- `acelerometro.c` - Implementación

### Características
- **3 funciones de generación de señales** por eje:
  - Armónica Simple: $a_1(t) = A \cdot \sin(2\pi ft)$
  - Modulada en Amplitud: $a_2(t) = A \cdot \cos(2\pi f_1t) \cdot \sin(2\pi f_2t)$
  - Multicomponente: $a_3(t) = \frac{2A}{2} \cdot [\sin(2\pi ft) + \cos(4\pi ft)]$

- **Amplitud (A)**: Configurable 4g, 8g, 16g
- **Frecuencia de Muestreo (fs)**: 50, 100, 200, 500, 1000 Hz
- **Control independiente** por eje (X, Y, Z)

### Funciones principales
```c
void acelerometro_init(Acelerometro3Axial *acc);
bool acelerometro_procesar_eje(EjeAcelerometro *eje, int64_t now_us, float *out_val);
```

---

## 2. Sensor de Variables Ambientales

### Archivos
- `varAmbientales.h` - Header con estructuras
- `varAmbientales.c` - Implementación

### Características

#### Temperatura
- **Rango**: 15.0 a 30.0 °C
- **Resolución**: 0.1 °C
- **Generación**: Aleatoria

#### Humedad Relativa
- **Rango**: 20% a 40%
- **Resolución**: 1%
- **Generación**: Aleatoria

#### Intervalo de Envío
- **Opciones**: 30 segundos o 60 segundos
- **Configurable dinámicamente** desde la GUI

### Funciones principales
```c
// Inicializar sensor (intervalo_s: 30 o 60)
void varAmbientales_init(SensorAmbientales *sensor, int intervalo_s);

// Procesar y generar nuevos valores
bool varAmbientales_procesar(SensorAmbientales *sensor, int64_t now_us, 
                              float *out_temp, float *out_hum);

// Cambiar intervalo dinámicamente
void varAmbientales_set_intervalo(SensorAmbientales *sensor, int intervalo_s);
```

---

## 3. Integración en main.c

### Estructura de Tareas FreeRTOS
Se han creado 2 tareas independientes que se ejecutan en paralelo:

```c
void task_acelerometro(void *pvParameters)
// - Genera datos del acelerómetro
// - Frecuencia: 1 ms (1000 Hz máximo)
// - Prioridad: 2
// - Core: 0

void task_varAmbientales(void *pvParameters)
// - Genera datos de temperatura y humedad
// - Frecuencia: 100 ms
// - Prioridad: 1
// - Core: 1
```

### Función app_main()
```c
void app_main(void)
// Crea ambas tareas en diferente core del ESP32 (dual-core)
// Permite procesamiento paralelo sin bloqueos
```

---

## 4. Estructura de Datos

### Acelerómetro
```c
typedef struct {
    int funcion;             // 1, 2, 3 (tipo de señal)
    float A;                 // Amplitud en 'g'
    int fs;                  // Frecuencia de muestreo (Hz)
    float t;                 // Tiempo acumulado (s)
    float f, f1, f2;         // Frecuencias (Hz)
    int64_t last_sample_us;  // Última muestra (microsegundos)
} EjeAcelerometro;

typedef struct {
    EjeAcelerometro x, y, z;
} Acelerometro3Axial;
```

### Variables Ambientales
```c
typedef struct {
    float temperatura;       // Valor actual en °C
    float humedad;           // Valor actual en %
    int intervalo_envio_s;   // 30 o 60 segundos
    int64_t last_sample_us;  // Última muestra
    uint32_t seed;           // Semilla aleatoria
} SensorAmbientales;
```

---

## 5. Uso Típico

### En una tarea (pseudocódigo)
```c
// Acelerómetro
Acelerometro3Axial acc;
float val_x, val_y, val_z;
acelerometro_init(&acc);

while(1) {
    int64_t now = esp_timer_get_time();
    if (acelerometro_procesar_eje(&acc.x, now, &val_x)) {
        // Usar val_x
    }
}

// Variables Ambientales
SensorAmbientales sensor;
float temp, humedad;
varAmbientales_init(&sensor, 30);  // Cada 30 segundos

while(1) {
    int64_t now = esp_timer_get_time();
    if (varAmbientales_procesar(&sensor, now, &temp, &humedad)) {
        // Usar temp y humedad
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

---

## 6. Configuración Dinámica

### Para cambiar parámetros en tiempo de ejecución:

**Acelerómetro:**
```c
acc.x.funcion = 2;  // Cambiar a modulada
acc.x.A = 8.0f;     // Cambiar amplitud a 8g
acc.x.fs = 200;     // Cambiar a 200 muestras/segundo
```

**Variables Ambientales:**
```c
varAmbientales_set_intervalo(&sensor, 60);  // Cambiar a 60 segundos
```

---

## 7. Notas Importantes

- ✅ El generador aleatorio usa **LCG** (Linear Congruential Generator) para compatibilidad
- ✅ En ESP32 real, reemplazar `lcg_rand()` por `esp_random()` para mejor aleatoriedad
- ✅ Las tareas se ejecutan en **dual-core** sin bloqueos
- ✅ Uso de `esp_timer_get_time()` en microsegundos para precisión
- ✅ Incluir printf() para debug (comentar en producción)

---

## 8. Próximos Pasos

1. ✅ Implementar UART Communication (commUART.C)
2. ✅ Integrar GUI para cambios dinámicos
3. ✅ Testar en ESP32 real
4. ✅ Optimizar stack size según necesidades

