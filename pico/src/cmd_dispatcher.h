/* Applies a parsed pitrac_cmd_t against a hardware vtable. Firmware passes
 * its real vtable; host tests pass a mock. */

#ifndef PITRAC_PICO_CMD_DISPATCHER_H
#define PITRAC_PICO_CMD_DISPATCHER_H

#include <stdbool.h>
#include <stdint.h>

#include "proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* DSP config */
    void    (*set_threshold)(int32_t);
    int32_t (*get_threshold)(void);
    /* int64: mic mean-square RMS exceeds INT32_MAX on a loud strike; an int32
     * return wrapped negative and silently passed the arm-quiet gate. */
    int64_t (*current_rms)(void);
    void    (*set_decay_confirm)(uint32_t ms);

    /* Strobe pattern. Pass intervals_ms=NULL to keep current intervals;
     * pass pulse_width_us=0.0f to keep current width. Returns false if the
     * combined pattern would overrun PIO memory or the train budget. */
    bool    (*set_pulse_train)(const float *intervals_ms,
                               uint8_t count,
                               float pulse_width_us);

    /* Arm / disarm. set_armed(true) also resets the auto-disarm deadline. */
    void    (*set_armed)(bool);
    void    (*set_arm_timeout)(uint32_t ms);

    /* Cam-XTR + cooldown timing */
    void    (*set_cam_xtr_setup)(uint32_t us);
    void    (*set_min_inter_shot)(uint32_t ms);
    void    (*set_pre_trigger_delay)(uint32_t ms);

    /* Strobe hold (LED current calibration). hold_assert returns false if
     * a fire is in flight and the assertion was refused. */
    bool    (*hold_assert)(void);
    void    (*hold_release)(void);

    /* Continuous mic RMS stream. 0 hz stops emission. */
    void    (*set_stream_rms_hz)(uint32_t hz);

    /* IPC + control flow */
    void    (*request_manual_fire)(void);
    /* request_manual_fire + samples ADC0 (GP26 → V3 CUR-SENSE) across the
     * strobe train, emits one EVENT with peak ADC. Pi-side calibration sweep. */
    void    (*request_fire_peak)(void);
    void    (*request_reset)(void);
    void    (*request_bootsel)(void);

    /* Session keep-alive: refresh the arm deadline without re-running the
     * arm-quiet gate. A no-op while disarmed. */
    void    (*heartbeat)(void);

    /* Cam2 XTR pulse without firing the strobe. M2 addition. */
    void    (*cam_pulse)(uint32_t microseconds);

    /* USB-CDC output */
    void    (*emit_log)(const char *msg);
    void    (*emit_status)(void);
    void    (*selftest)(void);
} hw_driver_t;

void cmd_dispatcher_apply(const pitrac_cmd_t *cmd, const hw_driver_t *hw);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_PICO_CMD_DISPATCHER_H */
