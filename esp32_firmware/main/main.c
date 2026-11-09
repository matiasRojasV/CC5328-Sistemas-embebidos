#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#include <driver/uart.h>
#include <esp_err.h>

//#include "sensores/acelerometro.h"
//#include "sensores/ambiente.h"
#include "sensores/bme688.h"

#define UART_PORT_NUM 0
#define BUF_SIZE 1024

// Marcador del protocolo binario (ESP -> Python)
#define DATA_MARKER "<{DP}>"

// Cpacidad del buffer circular de temperatura: 60 s max * 5Hz max = 300 muestras
#define MAX_TEMP_HISTORY 300

// Estrucuta para los samples de temperatura
typedef struct {
    int64_t t_ms; // timestamp en milisegundos
    float temp_c;   // temperatura 
} temp_sample_t;

#define MARKER_LEN  (sizeof(DATA_MARKER) - 1)
#define HEADER_LEN  (MARKER_LEN + sizeof(uint16_t))
#define PACKET_MAX_LEN (HEADER_LEN + 3 * sizeof(float))

// configuracion global protegida por config mutex

static SemaphoreHandle_t config_mutex;
static bool streaming_activo = false; // control de transmisión por software

static float sampling_hz = 1.0f; // frecuencia de muestreo en Hz, valores permitidos: 0.5, 1-0, 2.0 y 5.0 Hz
static bool tx_temp = true; // transmicion temperatura
static bool tx_hum = true; // transmicion humedad
static bool tx_press = true; // transmicion presion atmosferica

static bool avg_enabled = false; // toggle de promedio
static int avg_window_s = 10; // ventana de promedio en segundos, valores permitidos: 5, 10, 30, 60 s

static bool peak_enabled = false; // toggle de detección de picos
static float peak_thresh_c = 28.0f; // umbral de detección de picos (valor inicial), debe ser modificable

// Estado histprial de temperatura para el promedio y la detección de picos
static temp_sample_t temp_history[MAX_TEMP_HISTORY];
static int history_head = 0;
static int history_count = 0;
static int64_t first_sample_ms = -1;

// Estado para la deteccion de picos en las 3 muestras consecutivas (T_{k-2}, T_{k-1}, T_k)
static float t_hist_3[3] = {0.0f, 0.0f, 0.0f}; // buffer circular de 3 muestras
static int64_t t_hist_ms[3] = {0, 0, 0}; // timestamps de las 3 muestras
static uint32_t t_hist_seq[3] = {0, 0, 0}; // secuencias de las 3 muestras
static int t_hist_valid = 0; // cantidad de muestras válidas en el buffer circular
static uint32_t last_peak_seq_id = 0; // secuencia de la última muestra que generó un pico

// Función auxiliar para enviar cualquier linea precedida por el marcador <{DP}>
static void enviar_paquete_serial(const char *payload) {
    char out_buf[256];
    int len = snprintf(out_buf, sizeof(out_buf), "%s%s\r\n", DATA_MARKER, payload);
    if (len > 0 && len < (int)sizeof(out_buf)) {
        uart_write_bytes(UART_PORT_NUM, out_buf, len);
    }
}

// Funcion auxiliar para enviar los ACKs con el estado actual confirmado por el ESP32
static void enviar_ack(const char *cmd_name) {
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    char msg[192];
    snprintf(msg, sizeof(msg),
             "ACK,cmd=%s,fs=%.1f,T=%d,H=%d,P=%d,avg_en=%d,win_s=%d,peak_en=%d,thr_c=%.1f",
             cmd_name, sampling_hz,
             tx_temp ? 1 : 0, tx_hum ? 1 : 0, tx_press ? 1 : 0,
             avg_enabled ? 1 : 0, avg_window_s,
             peak_enabled ? 1 : 0, peak_thresh_c);
    xSemaphoreGive(config_mutex);
    enviar_paquete_serial(msg);
}

// Funcion auxiliar para enviar mensajes de rechazo/error ante comandos o parametros invalidos
static void enviar_error(const char *cmd_name, const char *error_msg) {
    char msg[128];
    snprintf(msg, sizeof(msg), "ERR,cmd=%s,msg=%s", cmd_name, error_msg);
    enviar_paquete_serial(msg);
}

// Funcion auxiliar para guardar una muestra de Temperatura en el buffer circular y actualiza el registro de 3 muestras
static void registrar_muestra_temp(float temp_c, int64_t now_ms, uint32_t seq_id) {
    if (first_sample_ms < 0) {
        first_sample_ms = now_ms;
    }

    temp_history[history_head].t_ms = now_ms;
    temp_history[history_head].temp_c = temp_c;
    history_head = (history_head + 1) % MAX_TEMP_HISTORY;
    if (history_count < MAX_TEMP_HISTORY) {
        history_count++;
    }

    // actualizar el buffer circular. indice 0 = k-2, indice 1 = k-1, indice 2 = k
    t_hist_3[0]   = t_hist_3[1];
    t_hist_ms[0]  = t_hist_ms[1];
    t_hist_seq[0] = t_hist_seq[1];

    t_hist_3[1]   = t_hist_3[2];
    t_hist_ms[1]  = t_hist_ms[2];
    t_hist_seq[1] = t_hist_seq[2];

    t_hist_3[2]   = temp_c;
    t_hist_ms[2]  = now_ms;
    t_hist_seq[2] = seq_id;

    if (t_hist_valid < 3) {
        t_hist_valid++;
    }
}

// Calcular el promedio temporal de los ultimos W segundos: t - W < t_i <= t
static float calcular_promedio_temporal(int64_t now_ms, int window_s, bool *out_window_complete) {
    int64_t window_ms = (int64_t)window_s * 1000;
    int64_t limit_ms = now_ms - window_ms;

    double sum = 0.0;
    int count = 0;

    for (int i = 0; i < history_count; i++) {
        int idx = (history_head - 1 - i + MAX_TEMP_HISTORY) % MAX_TEMP_HISTORY;
        if (temp_history[idx].t_ms > limit_ms && temp_history[idx].t_ms <= now_ms) {
            sum += temp_history[idx].temp_c;
            count++;
        }
    }

    if (first_sample_ms >= 0 && (now_ms - first_sample_ms) >= window_ms) {
        *out_window_complete = true;
    } else {
        *out_window_complete = false;
    }

    return (count > 0) ? (float)(sum / count) : 0.0f;
}

// Tarea de adquisicion y procesamiento local (TX)
void task_adquisicion_bme(void *pvParameters) {
    uint32_t seq = 0;

    while (1) {
        int64_t cycle_start_ms = esp_timer_get_time() / 1000; // timestamp en milisegundos
        
        xSemaphoreTake(config_mutex, portMAX_DELAY);
        bool activo = streaming_activo;
        float fs = sampling_hz; 
        bool send_t = tx_temp;
        bool send_h = tx_hum;
        bool send_p = tx_press;
        bool do_avg = avg_enabled;
        int win_s = avg_window_s;
        bool do_peak = peak_enabled;
        float thr_c = peak_thresh_c;

        xSemaphoreGive(config_mutex);

        int32_t period_ms = (int32_t)(1000.0f / fs);
        if (activo) {
            // regla seccion 2.2: adquirir T internamente si send_t, do_avg o do_peak están activos
            bool need_t = send_t || do_avg || do_peak;
            bme688_data_t meas; // meas de measurement

            esp_err_t err = bme688_read(need_t, send_h, send_p, &meas);

            int64_t now_ms = esp_timer_get_time() / 1000;

            if (err != ESP_OK) {
                // por si falla el sensor, se envia el error
                enviar_error("SENSOR", esp_err_to_name(err));
            } else {
                seq++;
                char sample_msg[220];
                int offset = snprintf(sample_msg, sizeof(sample_msg),
                                      "SAMPLE,seq=%lu,t_ms=%lld",
                                      (unsigned long)seq, (long long)now_ms);

                // agregar unicamente las variables seleccionadas para transmicion (7 posibles combinaciones)
                if (send_t) {
                    offset += snprintf(sample_msg + offset, sizeof(sample_msg) - offset,
                                       ",T=%.2f", meas.temperature_c);
                }
                if (send_h) {
                    offset += snprintf(sample_msg + offset, sizeof(sample_msg) - offset,
                                       ",H=%.2f", meas.humidity_rh);
                }
                if (send_p) {
                    offset += snprintf(sample_msg + offset, sizeof(sample_msg) - offset,
                                       ",P=%.2f", meas.pressure_hpa);
                }

                if (need_t) {
                    registrar_muestra_temp(meas.temperature_c, now_ms, seq);

                    // promedio movil temporal (temp_avg_c)
                    if (do_avg) {
                        bool win_full = false;
                        float temp_avg = calcular_promedio_temporal(now_ms, win_s, &win_full);
                        offset += snprintf(sample_msg + offset, sizeof(sample_msg) - offset,
                                           ",temp_avg_c=%.2f,win_full=%d",
                                           temp_avg, win_full ? 1 : 0);
                    }

                    // enviamos la muestra periodica antes de posibles eventos
                    enviar_paquete_serial(sample_msg);

                    // deteccion de pico local: T_{k-1} > T_{k-2}, T_{k-1} >= T_k, T_{k-1} >= thr_c
                    if (do_peak && t_hist_valid >= 3) {
                        float t_km2 = t_hist_3[0];
                        float t_km1 = t_hist_3[1];
                        float t_k = t_hist_3[2];
                        uint32_t peak_seq = t_hist_seq[1];

                        if (t_km1 > t_km2 && t_km1 >= t_k && t_km1 >= thr_c && peak_seq != last_peak_seq_id) {
                            last_peak_seq_id = peak_seq;
                            char peak_msg[128];
                            snprintf(peak_msg, sizeof(peak_msg),
                                     "PEAK,t_ms=%lld,peak_c=%.2f,thr_c=%.1f",
                                     (long long)t_hist_ms[1], t_km1, thr_c);
                            enviar_paquete_serial(peak_msg);
                        }

                    }

                } else {
                    // en caso de que solo se transmite H y/o P sin procesamiento de T activo
                    enviar_paquete_serial(sample_msg);
                }
            }
        }
        // compensa el tiempo que tomo la conversion I2C para respetar la frecuencia comun (0.5, 1, 2, 5 Hz)
        int64_t elapsed_ms = (esp_timer_get_time() / 1000) - cycle_start_ms;
        int32_t sleep_ms = period_ms - (int32_t)elapsed_ms;
        if (sleep_ms < 10) {
            sleep_ms = 10; // minimo 10 ms de espera para no saturar el bus I2C
        }
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));

    }
}

// funcion que valida si la frecuencia es una de las que son permitidas: 0.5, 1.0, 2.0, 5.0 Hz
static bool es_frecuencia_valida(float fs) {
    return (fabsf(fs - 0.5f) < 0.05f ||
            fabsf(fs - 1.0f) < 0.05f ||
            fabsf(fs - 2.0f) < 0.05f ||
            fabsf(fs - 5.0f) < 0.05f);
}


// Tarea Recepción de Comandos (RX) V.T2 - Python a ESP32 en modo texto
void task_uart_rx(void *pvParameters) {
    uint8_t rx_chunk[128];
    char line[BUF_SIZE];
    int line_pos = 0;

    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, rx_chunk, sizeof(rx_chunk), pdMS_TO_TICKS(50));

        for (int i = 0; i < len; i++) {
            char c = (char)rx_chunk[i];
            
            // Solo procesamos el comando cuando llega un fin de linea (enter, \r, \n)
            if (c == '\r' || c == '\n') {
                if (line_pos > 0) {
                    line[line_pos] = '\0';
                    line_pos = 0;

                    // Comando START
                    if (strncmp(line, "START", 5) == 0) {
                        xSemaphoreTake(config_mutex, portMAX_DELAY);
                        streaming_activo = true;
                        xSemaphoreGive(config_mutex);
                        enviar_ack("START");
                        //printf("Transmisión activada\n");
                    }
                    // Comando STOP
                    else if (strncmp(line, "STOP", 4) == 0) {
                        xSemaphoreTake(config_mutex, portMAX_DELAY);
                        streaming_activo = false;
                        xSemaphoreGive(config_mutex);
                        enviar_ack("STOP");
                        //printf("Transmisión pausada\n");
                    }
                    // Comando SET_ACC,<fs>,<t>,<h>,<p>
                    else if (strncmp(line, "SET_ACQ", 7) == 0) {
                        float fs;
                        int t, h, p;
                        if (sscanf(line, "SET_ACQ,%f,%d,%d,%d", &fs, &t, &h, &p) == 4) {
                            if (!es_frecuencia_valida(fs)) {
                                enviar_error("SET_ACQ", "frecuencia_invalida");
                            } else if (t==0 && h==0 && p==0) {
                                enviar_error("SET_ACQ", "seleccion_vacia");
                            } else {
                                xSemaphoreTake(config_mutex, portMAX_DELAY);
                                sampling_hz = fs;
                                tx_temp = (t != 0);
                                tx_hum = (h != 0);
                                tx_press = (p != 0);
                                xSemaphoreGive(config_mutex);
                                enviar_ack("SET_ACQ");
                            }
                        } else {
                            enviar_error("SET_ACQ", "formato_invalido");
                        }
                    }
                    // Comando SET_AVG,<en>,<win_s>
                    else if (strncmp(line, "SET_AVG", 7) == 0) {
                        int en, win_s;
                        if (sscanf(line, "SET_AVG,%d,%d", &en, &win_s) == 2) {
                            if (win_s != 5 && win_s != 10 && win_s != 30 && win_s != 60) {
                                enviar_error("SET_AVG", "ventana_invalida");
                            } else {
                                xSemaphoreTake(config_mutex, portMAX_DELAY);
                                avg_enabled  = (en != 0);
                                avg_window_s = win_s;
                                xSemaphoreGive(config_mutex);
                                enviar_ack("SET_AVG");
                            }
                        } else {
                            enviar_error("SET_AVG", "formato_invalido");
                        }
                    }
                    // Comando SET_PEAK,<en>,<thr_c>
                    else if (strncmp(line, "SET_PEAK", 8) == 0) {
                        int en;
                        float thr_c;
                        if (sscanf(line, "SET_PEAK,%d,%f", &en, &thr_c) == 2) {
                            if (thr_c < -40.0f || thr_c > 85.0f) {
                                enviar_error("SET_PEAK", "umbral_fuera_de_rango");
                            } else {
                                xSemaphoreTake(config_mutex, portMAX_DELAY);
                                peak_enabled = (en != 0);
                                peak_thresh_c   = thr_c;
                                xSemaphoreGive(config_mutex);
                                enviar_ack("SET_PEAK");
                            }
                        } else {
                            enviar_error("SET_PEAK", "formato_invalido");
                        }
                    }
                    // comando desconocido
                    else {
                        enviar_error("UNKNOWN", "comando_desconocido");
                    }
                }
        } else if (line_pos < (BUF_SIZE - 1)) {
                line[line_pos++] = c;
            }
        }
    }
}

void app_main(void) {
    // 1. Configuración UART a 115200 baudios (usando UART_PIN_NO_CHANGE para compatibilidad con la ESP32-S3)
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
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    config_mutex = xSemaphoreCreateMutex();

    // 2. Inicializamos el sensor BME688 por I2C (si falla al arrancar, no bloquea el sistema y avisa por serial)
    esp_err_t err = bme688_init();
    if (err != ESP_OK) {
        enviar_error("INIT_BME688", esp_err_to_name(err));
    }

    // 3. Iniciar tareas FreeRTOS
    xTaskCreate(task_uart_rx, "task_uart_rx", 4096, NULL, 10, NULL);
    xTaskCreate(task_adquisicion_bme, "task_adquisicion_bme", 4096, NULL, 5, NULL);
}