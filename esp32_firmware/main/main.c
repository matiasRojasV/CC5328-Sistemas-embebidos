#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#include <driver/uart.h>

#include "sensores/acelerometro.h"
#include "sensores/ambiente.h"

#define UART_PORT_NUM 0
#define BUF_SIZE (1024)

// Marcador del protocolo binario (ESP -> Python)
#define DATA_MARKER "<{DP}>"
#define MARKER_LEN  (sizeof(DATA_MARKER) - 1)
#define HEADER_LEN  (MARKER_LEN + sizeof(uint16_t))
#define PACKET_MAX_LEN (HEADER_LEN + 3 * sizeof(float))

// Variables globales protegidas
static Acelerometro3Axial acc;
static SensorAmbientales sensor_env;
static SemaphoreHandle_t config_mutex;

// Control de transmisión por software
static bool streaming_activo = false;


// Función auxiliar para empaquetar y enviar floats
void enviar_datos_binarios(float* values, uint16_t float_num) {
    uint8_t buf[PACKET_MAX_LEN];
    size_t len = 0;
    
    // 1. Marcador
    memcpy(buf + len, DATA_MARKER, MARKER_LEN);
    len += MARKER_LEN;
    // 2. Cantidad de floats
    memcpy(buf + len, &float_num, sizeof(float_num));
    len += sizeof(float_num);
    // 3. Arreglo de floats
    memcpy(buf + len, values, float_num * sizeof(float));
    len += float_num * sizeof(float);

    uart_write_bytes(UART_PORT_NUM, buf, len);
}

// Tarea Acelerómetro (TX)
void task_acelerometro(void *pvParameters) {
    float val_x = 0.0f, val_y = 0.0f, val_z = 0.0f;
    float values[3];

    while (1) {
        int64_t now_us = esp_timer_get_time();

        xSemaphoreTake(config_mutex, portMAX_DELAY);
        bool activo = streaming_activo;
        bool new_x = acelerometro_procesar_eje(&acc.x, now_us, &val_x);
        bool new_y = acelerometro_procesar_eje(&acc.y, now_us, &val_y);
        bool new_z = acelerometro_procesar_eje(&acc.z, now_us, &val_z);
        xSemaphoreGive(config_mutex);

        if (activo && new_x && new_y && new_z) {
            values[0] = val_x;
            values[1] = val_y;
            values[2] = val_z;
            enviar_datos_binarios(values, 3);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Tarea Variables Ambientales (TX)
void task_ambientales(void *pvParameters) {
    float temp = 0.0f, humedad = 0.0f;
    float values[2];

    while (1) {
        int64_t now_us = esp_timer_get_time();

        xSemaphoreTake(config_mutex, portMAX_DELAY);
        bool activo = streaming_activo;
        bool new_data = varAmbientales_procesar(&sensor_env, now_us, &temp, &humedad);
        xSemaphoreGive(config_mutex);

        if (activo && new_data) {
            values[0] = temp;
            values[1] = humedad;
            enviar_datos_binarios(values, 2); // Enviamos 2 floats
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Tarea Recepción de Comandos (RX) - Python a ESP32 en modo texto
void task_uart_rx(void *pvParameters) {
    uint8_t data[BUF_SIZE];
    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, data, BUF_SIZE - 1, pdMS_TO_TICKS(50));
        if (len > 0) {
            data[len] = '\0';

            char *line = strtok((char*)data, "\r\n");
            while (line != NULL) {
                // Comando START
                if (strncmp((char*)data, "START", 5) == 0) {
                    xSemaphoreTake(config_mutex, portMAX_DELAY);
                    streaming_activo = true;
                    xSemaphoreGive(config_mutex);
                    printf("Transmisión activada\n");
                }

                // Comando STOP
                else if (strncmp((char*)data, "STOP", 4) == 0) {
                    xSemaphoreTake(config_mutex, portMAX_DELAY);
                    streaming_activo = false;
                    xSemaphoreGive(config_mutex);
                    printf("Transmisión pausada\n");
                }

                // Comando SET_ACC
                else if (strncmp((char*)data, "SET_ACC", 7) == 0) {
                    char eje; int func, amp, fs;
                    if (sscanf((char*)data, "SET_ACC, %c, %d, %d, %d", &eje, &func, &amp, &fs) == 4) {
                        EjeAcelerometro *target = NULL;

                        // Convierte el carácter a mayúscula 
                        switch (toupper((unsigned char)eje)) {
                            case 'X': target = &acc.x; break;
                            case 'Y': target = &acc.y; break;
                            case 'Z': target = &acc.z; break;
                            default:  target = NULL;   break; // Ignora caracteres no válidos
                        }

                        // Solo aplica los cambios si el eje fue reconocido correctamente
                        if (target != NULL) {
                            xSemaphoreTake(config_mutex, portMAX_DELAY);
                            target->funcion = func;
                            target->A = (float)amp;
                            target->fs = fs;
                            xSemaphoreGive(config_mutex);
                        }
                    }
                }

                // Comando SET_ENV
                else if (strncmp((char*)data, "SET_ENV", 7) == 0) {
                    int intervalo;
                    if (sscanf((char*)data, "SET_ENV, %d", &intervalo) == 1) {
                        xSemaphoreTake(config_mutex, portMAX_DELAY);
                        varAmbientales_set_intervalo(&sensor_env, intervalo);
                        xSemaphoreGive(config_mutex);
                    }
                }
                line = strtok(NULL, "\r\n");
            }
        }

    }
}

void app_main(void) {
    // 1. Configuración UART 
    uart_config_t uart_config = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    config_mutex = xSemaphoreCreateMutex();
    acelerometro_init(&acc);
    varAmbientales_init(&sensor_env, 30);

    // 2. Iniciar tareas
    xTaskCreate(task_uart_rx, "uart_rx_task", 4096, NULL, 10, NULL);
    xTaskCreate(task_acelerometro, "acelerometro_task", 4096, NULL, 5, NULL);
    xTaskCreate(task_ambientales, "varambientales_task", 4096, NULL, 5, NULL);
}