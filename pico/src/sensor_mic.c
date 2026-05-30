#include "sensor.h"
#include "sensor_mic.h"

#include <stdio.h>

#include "impact_detect.h"

static ring_buffer_t *s_ring = NULL;

void sensor_mic_set_ring(ring_buffer_t *ring) {
    s_ring = ring;
}

static bool mic_init(void) {
    if (s_ring == NULL) return false;
    impact_detect_init(s_ring);
    return true;
}

static bool mic_step(bool armed, int32_t *level_out) {
    int32_t rms = 0;
    bool fired = impact_detect_step(armed, &rms);
    if (level_out) *level_out = rms;
    return fired;
}

static int64_t mic_current_level(void) {
    return impact_detect_current_rms();
}

static void mic_selftest_append(char *buf, int buflen, int *cursor) {
    if (*cursor >= buflen) return;
    int k = snprintf(buf + *cursor, buflen - *cursor,
                     " mic_rms=%lld", (long long)impact_detect_current_rms());
    if (k > 0) *cursor += k;
}

const sensor_t g_sensor_mic = {
    .name = "mic",
    .init = mic_init,
    .step = mic_step,
    .current_level = mic_current_level,
    .selftest_append = mic_selftest_append,
};
