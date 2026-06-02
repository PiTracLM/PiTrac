#include "sensor.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/* Trigger detectors. Mic (acoustic impact) is the only live one; optical is a
 * template. sensors_step OR-combines fire flags, so any detector triggers. */
extern const sensor_t g_sensor_mic;
extern const sensor_t g_sensor_optical;

static const sensor_t *const s_sensors[] = {
    &g_sensor_mic,
    &g_sensor_optical,
};
static const uint8_t s_sensor_count = sizeof(s_sensors) / sizeof(s_sensors[0]);

bool sensors_init_all(void) {
    bool all_ok = true;
    for (uint8_t i = 0; i < s_sensor_count; ++i) {
        if (s_sensors[i]->init && !s_sensors[i]->init()) all_ok = false;
    }
    return all_ok;
}

bool sensors_step(bool armed, int32_t *peak_level_out) {
    bool any_fired = false;
    int32_t peak = 0;
    for (uint8_t i = 0; i < s_sensor_count; ++i) {
        int32_t lvl = 0;
        if (s_sensors[i]->step && s_sensors[i]->step(armed, &lvl)) any_fired = true;
        if (lvl > peak) peak = lvl;
    }
    if (peak_level_out) *peak_level_out = peak;
    return any_fired;
}

int64_t sensors_max_level(void) {
    int64_t peak = 0;
    for (uint8_t i = 0; i < s_sensor_count; ++i) {
        if (!s_sensors[i]->current_level) continue;
        int64_t lvl = s_sensors[i]->current_level();
        if (lvl > peak) peak = lvl;
    }
    return peak;
}

void sensors_selftest_append(char *buf, int buflen, int *cursor) {
    for (uint8_t i = 0; i < s_sensor_count; ++i) {
        if (s_sensors[i]->selftest_append) {
            s_sensors[i]->selftest_append(buf, buflen, cursor);
        }
    }
}

void sensor_selftest_printf(char *buf, int buflen, int *cursor, const char *fmt, ...) {
    if (buf == NULL || cursor == NULL || *cursor >= buflen) return;
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(buf + *cursor, (size_t)(buflen - *cursor), fmt, ap);
    va_end(ap);
    if (k > 0) *cursor += k;
}
