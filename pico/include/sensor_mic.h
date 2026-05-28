#ifndef PITRAC_PICO_SENSOR_MIC_H
#define PITRAC_PICO_SENSOR_MIC_H

#include "ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

void sensor_mic_set_ring(ring_buffer_t *ring);

#ifdef __cplusplus
}
#endif

#endif
