#include "ambiente.h"
#include <stdlib.h>
#include <time.h>

static uint32_t lcg_rand(uint32_t *seed) {
    *seed = (1103515245 * (*seed) + 12345) & 0x7fffffff;
    return *seed;
}

void varAmbientales_init(SensorAmbientales *sensor, int intervalo_s) {
    sensor->temperatura = 22.0f;
    sensor->humedad = 30.0f;
    sensor->intervalo_envio_s = (intervalo_s == 60) ? 60 : 30;
    sensor->last_sample_us = 0;
    sensor->seed = (uint32_t)time(NULL);
}

bool varAmbientales_procesar(SensorAmbientales *sensor, int64_t now_us, float *out_temp, float *out_hum) {
    int64_t period_us = (int64_t)sensor->intervalo_envio_s * 1000000;

    if ((now_us - sensor->last_sample_us) >= period_us) {
        sensor->last_sample_us = now_us;

        uint32_t temp_decimas = 150 + (lcg_rand(&sensor->seed) % 151);
        *out_temp = temp_decimas / 10.0f;

        uint32_t humedad_val = 20 + (lcg_rand(&sensor->seed) % 21);
        *out_hum = (float)humedad_val;

        sensor->temperatura = *out_temp;
        sensor->humedad = *out_hum;
        return true;
    }

    return false;
}

void varAmbientales_set_intervalo(SensorAmbientales *sensor, int intervalo_s) {
    sensor->intervalo_envio_s = (intervalo_s == 60) ? 60 : 30;
    sensor->last_sample_us = 0;
}