#ifndef PITRAC_PICO_INIT_STAGE_H
#define PITRAC_PICO_INIT_STAGE_H

#include <assert.h>

typedef enum {
    INIT_STAGE_NONE = 0,
    INIT_STAGE_RUNTIME_STATE,
    INIT_STAGE_PRECLAIM,
    INIT_STAGE_LED,
    INIT_STAGE_STDIO,
    INIT_STAGE_STROBE_PIO,
    INIT_STAGE_STROBE_PATTERN,
    INIT_STAGE_I2S,
    INIT_STAGE_IMPACT,
    INIT_STAGE_M2_PINS,
    INIT_STAGE_CORE1,
    INIT_STAGE_WATCHDOG,
} init_stage_t;

extern volatile init_stage_t g_init_stage;

static inline void init_stage_advance(init_stage_t expected, init_stage_t next) {
    assert(g_init_stage == expected);
    g_init_stage = next;
}

#endif
