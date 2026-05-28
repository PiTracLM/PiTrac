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
    int32_t (*current_level)(void);
    void    (*selftest_append)(char *buf, int buflen, int *cursor);
} sensor_t;

bool    sensors_init_all(void);
bool    sensors_step(bool armed, int32_t *peak_level_out);
int32_t sensors_max_level(void);
void    sensors_selftest_append(char *buf, int buflen, int *cursor);

#ifdef __cplusplus
}
#endif

#endif
