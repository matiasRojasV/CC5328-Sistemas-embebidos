#ifndef ACELEROMETRO_H
#define ACELEROMETRO_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int funcion;
    float A;
    int fs;
    float t;
    float f;
    float f1;
    float f2;
    int64_t last_sample_us;
} EjeAcelerometro;

typedef struct {
    EjeAcelerometro x;
    EjeAcelerometro y;
    EjeAcelerometro z;
} Acelerometro3Axial;

void acelerometro_init(Acelerometro3Axial *acc);
bool acelerometro_procesar_eje(EjeAcelerometro *eje, int64_t now_us, float *out_val);

#endif