#ifndef PITRAC_PICO_SENSOR_H
#define PITRAC_PICO_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sensor {
    const char *name;
    bool    (*init)(void);
    bool    (*step)(bool armed, int32_t *level_out);
    /* int64: mic high-band RMS (mean-square) overflows int32 on a loud strike
     * (see impact_detect.h); arm-quiet gate compares against it, so the getter
     * chain stays 64-bit or a negative wrap lets a loud room arm. Per-step
     * level_out is a separate int32 path. */
    int64_t (*current_level)(void);
    void    (*selftest_append)(char *buf, int buflen, int *cursor);
} sensor_t;

bool    sensors_init_all(void);
bool    sensors_step(bool armed, int32_t *peak_level_out);
int64_t sensors_max_level(void);
void    sensors_selftest_append(char *buf, int buflen, int *cursor);

/* Append a printf-formatted token at *cursor, bounded by buflen, advancing it.
 * Shared by every sensor's selftest_append. */
void    sensor_selftest_printf(char *buf, int buflen, int *cursor, const char *fmt, ...)
            __attribute__((format(printf, 4, 5)));

#ifdef __cplusplus
}
#endif

#endif
