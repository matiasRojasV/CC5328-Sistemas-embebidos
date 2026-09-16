// Archivo: ejemplos_uso.c
// Este archivo contiene ejemplos de uso de los sensores
// Incluirlo o adaptar en tu proyecto según necesites

#include "sensores/acelerometro.h"
#include "sensores/ambiente.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// ==================== EJEMPLO 1: Configuración Básica ====================

void ejemplo_acelerometro_basico(void) {
    Acelerometro3Axial acc;
    acelerometro_init(&acc);
    
    // Configuración por defecto (preestablecida en acelerometro_init):
    // - Eje X: función=1, A=4g, fs=100Hz, f=5Hz, f1=2Hz, f2=10Hz
    // - Eje Y: función=1, A=4g, fs=100Hz, f=8Hz, f1=3Hz, f2=12Hz
    // - Eje Z: función=1, A=4g, fs=100Hz, f=12Hz, f1=4Hz, f2=15Hz
    
    float val_x, val_y, val_z;
    
    while (1) {
        int64_t now_us = esp_timer_get_time();
        
        bool new_x = acelerometro_procesar_eje(&acc.x, now_us, &val_x);
        bool new_y = acelerometro_procesar_eje(&acc.y, now_us, &val_y);
        bool new_z = acelerometro_procesar_eje(&acc.z, now_us, &val_z);
        
        if (new_x || new_y || new_z) {
            printf("ACC: X=%.3f Y=%.3f Z=%.3f g\n", val_x, val_y, val_z);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ==================== EJEMPLO 2: Configuración Personalizada ====================

void ejemplo_acelerometro_personalizado(void) {
    Acelerometro3Axial acc;
    acelerometro_init(&acc);
    
    // PERSONALIZAR CONFIGURACIÓN DE CADA EJE
    
    // Eje X: Armónica simple a 8g y 500 Hz
    acc.x.funcion = 1;
    acc.x.A = 8.0f;      // Cambiar a 8g
    acc.x.fs = 500;      // Cambiar a 500 Hz
    acc.x.f = 10.0f;     // Frecuencia principal
    
    // Eje Y: Modulada en amplitud a 16g y 1000 Hz
    acc.y.funcion = 2;
    acc.y.A = 16.0f;     // Cambiar a 16g
    acc.y.fs = 1000;     // Máxima frecuencia
    acc.y.f1 = 5.0f;
    acc.y.f2 = 25.0f;
    
    // Eje Z: Multicomponente a 4g y 200 Hz
    acc.z.funcion = 3;
    acc.z.A = 4.0f;
    acc.z.fs = 200;
    acc.z.f = 15.0f;
    
    float val_x, val_y, val_z;
    
    while (1) {
        int64_t now_us = esp_timer_get_time();
        
        acelerometro_procesar_eje(&acc.x, now_us, &val_x);
        acelerometro_procesar_eje(&acc.y, now_us, &val_y);
        acelerometro_procesar_eje(&acc.z, now_us, &val_z);
        
        printf("X(1-500Hz)=%.3f Y(2-1000Hz)=%.3f Z(3-200Hz)=%.3f\n", 
               val_x, val_y, val_z);
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ==================== EJEMPLO 3: Variables Ambientales Básico ====================

void ejemplo_varambientales_basico(void) {
    SensorAmbientales sensor;
    varAmbientales_init(&sensor, 30);  // Intervalo de 30 segundos
    
    float temp, humedad;
    
    while (1) {
        int64_t now_us = esp_timer_get_time();
        
        if (varAmbientales_procesar(&sensor, now_us, &temp, &humedad)) {
            printf("TEMPERATURA: %.1f°C, HUMEDAD: %.0f%%\n", temp, humedad);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ==================== EJEMPLO 4: Cambio Dinámico de Intervalo ====================

void ejemplo_varambientales_dinamico(void) {
    SensorAmbientales sensor;
    varAmbientales_init(&sensor, 30);  // Comenzar con 30 segundos
    
    float temp, humedad;
    int cambios = 0;
    
    while (1) {
        int64_t now_us = esp_timer_get_time();
        
        if (varAmbientales_procesar(&sensor, now_us, &temp, &humedad)) {
            printf("T=%.1f°C H=%.0f%% (intervalo: %d s)\n", 
                   temp, humedad, sensor.intervalo_envio_s);
            
            cambios++;
            // Cambiar a 60 segundos después de 5 lecturas
            if (cambios == 5) {
                varAmbientales_set_intervalo(&sensor, 60);
                printf(">>> Intervalo cambió a 60 segundos\n");
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ==================== EJEMPLO 5: Tarea Combinada ====================

void task_sensores_combinados(void *pvParameters) {
    // Inicializar acelerómetro
    Acelerometro3Axial acc;
    acelerometro_init(&acc);
    
    // Personalizar si es necesario
    acc.x.fs = 200;  // Aumentar frecuencia de X a 200 Hz
    
    // Inicializar sensor ambiental
    SensorAmbientales sensor;
    varAmbientales_init(&sensor, 30);
    
    float acc_x, acc_y, acc_z;
    float temp, humedad;
    int contador = 0;
    
    while (1) {
        int64_t now_us = esp_timer_get_time();
        
        // PROCESAR ACELERÓMETRO (Alta frecuencia)
        bool new_acc_x = acelerometro_procesar_eje(&acc.x, now_us, &acc_x);
        bool new_acc_y = acelerometro_procesar_eje(&acc.y, now_us, &acc_y);
        bool new_acc_z = acelerometro_procesar_eje(&acc.z, now_us, &acc_z);
        
        if (new_acc_x || new_acc_y || new_acc_z) {
            // Aquí: enviar por UART, escribir en archivo, etc.
            contador++;
        }
        
        // PROCESAR VARIABLES AMBIENTALES (Baja frecuencia)
        if (varAmbientales_procesar(&sensor, now_us, &temp, &humedad)) {
            printf("[CICLO %d] ACC(X=%.3f Y=%.3f Z=%.3f g) TEMP=%.1f°C HUM=%.0f%%\n",
                   contador, acc_x, acc_y, acc_z, temp, humedad);
        }
        
        // Tick a 1 ms para soportar acelerómetro a 1000 Hz
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ==================== TABLA DE REFERENCIA ====================
/*
TIPOS DE FUNCIÓN DEL ACELERÓMETRO:
┌─────┬──────────────────────────────────────────────────┐
│ ID  │ Fórmula                                          │
├─────┼──────────────────────────────────────────────────┤
│  1  │ a(t) = A * sin(2π*f*t)                           │
│     │ ARMÓNICA SIMPLE - Señal sinusoidal pura         │
├─────┼──────────────────────────────────────────────────┤
│  2  │ a(t) = A * cos(2π*f1*t) * sin(2π*f2*t)          │
│     │ MODULADA EN AMPLITUD - Envolvente de coseno    │
├─────┼──────────────────────────────────────────────────┤
│  3  │ a(t) = A * [sin(2π*f*t) + cos(4π*f*t)]          │
│     │ MULTICOMPONENTE - Dos frecuencias combinadas   │
└─────┴──────────────────────────────────────────────────┘

AMPLITUDES SOPORTADAS:
- 4.0f  (4 g)
- 8.0f  (8 g)
- 16.0f (16 g)

FRECUENCIAS DE MUESTREO SOPORTADAS:
- 50    Hz
- 100   Hz (default)
- 200   Hz
- 500   Hz
- 1000  Hz (máxima)

INTERVALOS DE ENVÍO (Variables Ambientales):
- 30  segundos
- 60  segundos (1 minuto)

RANGO DE TEMPERATURA:
- 15.0 a 30.0 °C (resolución 0.1°C)

RANGO DE HUMEDAD:
- 20 a 40 % (resolución 1%)
*/
