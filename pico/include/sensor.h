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
    /* current_level is int64 because the mic's high-band RMS is a mean-square
     * energy that overflows int32 on a loud strike (see impact_detect.h). The
     * arm-quiet gate compares against it, so the whole getter chain stays
     * 64-bit to avoid the negative wrap that used to let a loud room sneak an
     * arm through. The per-step `level_out` is a separate (still int32) path. */
    int64_t (*current_level)(void);
    void    (*selftest_append)(char *buf, int buflen, int *cursor);
} sensor_t;

bool    sensors_init_all(void);
bool    sensors_step(bool armed, int32_t *peak_level_out);
int64_t sensors_max_level(void);
void    sensors_selftest_append(char *buf, int buflen, int *cursor);

#ifdef __cplusplus
}
#endif

#endif
