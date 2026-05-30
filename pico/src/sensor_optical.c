#include "sensor.h"

#include <stdio.h>

static bool optical_init(void) {
    return true;
}

static bool optical_step(bool armed, int32_t *level_out) {
    (void)armed;
    if (level_out) *level_out = 0;
    return false;
}

static int64_t optical_current_level(void) {
    return 0;
}

static void optical_selftest_append(char *buf, int buflen, int *cursor) {
    if (*cursor >= buflen) return;
    int k = snprintf(buf + *cursor, buflen - *cursor, " optical=disabled");
    if (k > 0) *cursor += k;
}

const sensor_t g_sensor_optical = {
    .name = "optical",
    .init = optical_init,
    .step = optical_step,
    .current_level = optical_current_level,
    .selftest_append = optical_selftest_append,
};
