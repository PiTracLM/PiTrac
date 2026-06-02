#include "sensor.h"

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
    sensor_selftest_printf(buf, buflen, cursor, " optical=disabled");
}

const sensor_t g_sensor_optical = {
    .name = "optical",
    .init = optical_init,
    .step = optical_step,
    .current_level = optical_current_level,
    .selftest_append = optical_selftest_append,
};
