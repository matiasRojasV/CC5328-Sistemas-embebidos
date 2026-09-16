#include "acelerometro.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void acelerometro_init(Acelerometro3Axial *acc) {
    acc->x = (EjeAcelerometro){ .funcion = 1, .A = 4.0f, .fs = 100, .t = 0.0f, .f = 5.0f,  .f1 = 2.0f, .f2 = 10.0f, .last_sample_us = 0 };
    acc->y = (EjeAcelerometro){ .funcion = 1, .A = 4.0f, .fs = 100, .t = 0.0f, .f = 8.0f,  .f1 = 3.0f, .f2 = 12.0f, .last_sample_us = 0 };
    acc->z = (EjeAcelerometro){ .funcion = 1, .A = 4.0f, .fs = 100, .t = 0.0f, .f = 12.0f, .f1 = 4.0f, .f2 = 15.0f, .last_sample_us = 0 };
}

bool acelerometro_procesar_eje(EjeAcelerometro *eje, int64_t now_us, float *out_val) {
    int64_t period_us = 1000000 / eje->fs;

    if ((now_us - eje->last_sample_us) >= period_us) {
        float dt = 1.0f / (float)eje->fs;
        eje->t += dt;
        eje->last_sample_us = now_us;

        if (eje->t > 3600.0f) eje->t = 0.0f;

        switch (eje->funcion) {
            case 1:
                *out_val = eje->A * sinf(2.0f * (float)M_PI * eje->f * eje->t);
                break;

            case 2:
                *out_val = eje->A * cosf(2.0f * (float)M_PI * eje->f1 * eje->t) *
                           sinf(2.0f * (float)M_PI * eje->f2 * eje->t);
                break;

            case 3:
                *out_val = ((2.0f * eje->A) / 2.0f) * (sinf(2.0f * (float)M_PI * eje->f * eje->t) +
                                                      cosf(4.0f * (float)M_PI * eje->f * eje->t));
                break;

            default:
                *out_val = 0.0f;
                break;
        }
        return true;
    }
    return false;
}