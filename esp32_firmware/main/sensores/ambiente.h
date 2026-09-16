#ifndef AMBIENTE_H
#define AMBIENTE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float temperatura;
    float humedad;
    int intervalo_envio_s;
    int64_t last_sample_us;
    uint32_t seed;
} SensorAmbientales;

void varAmbientales_init(SensorAmbientales *sensor, int intervalo_s);
bool varAmbientales_procesar(SensorAmbientales *sensor, int64_t now_us, float *out_temp, float *out_hum);
void varAmbientales_set_intervalo(SensorAmbientales *sensor, int intervalo_s);

#endif