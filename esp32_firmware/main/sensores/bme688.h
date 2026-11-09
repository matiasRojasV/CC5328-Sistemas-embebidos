#ifndef BME688_H
#define BME688_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#define BME688_SDA_PIN          15
#define BME688_SCL_PIN          16
#define BME688_I2C_SLAVE_ADDR   0x76

typedef struct {
    float temperature_c; // Temperatura en °C
    float humidity_rh;   // Humedad relativa en %HR
    float pressure_hpa;  // Presión atmosférica en hPa
} bme688_data_t;

esp_err_t bme688_init(void);
esp_err_t bme688_read(bool req_t, bool req_h, bool req_p, bme688_data_t *out);

#endif 