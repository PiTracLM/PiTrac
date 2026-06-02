/*
 * proto.h — USB CDC command/event protocol between the Pi 5 host and the Pico.
 * Line-delimited ASCII; header-only so the host side can include it too.
 *
 * Wire format:
 *   - One command per line, '\n'-terminated. '\r' tolerated and stripped.
 *   - Case-sensitive. Numbers via strtof/strtol.
 *   - Lists comma-separated, no spaces; whitespace inside a value is undefined.
 *
 * Text not binary: drive it by hand over picocom, and no length-prefix /
 * endianness footguns. USB CDC bandwidth is ample (~hundreds of bytes/shot).
 */

#ifndef PITRAC_PICO_PROTO_H
#define PITRAC_PICO_PROTO_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------- commands -- */

/* Host commands; dispatcher in core1_usb.c switches on kind. */
typedef enum {
    CMD_NONE          = 0,
    CMD_CFG_THRESHOLD,        /* CFG MIC_THRESHOLD=<int>            */
    CMD_CFG_INTERVALS,        /* CFG PULSE_INTERVALS=<csv-floats>   */
    CMD_CFG_PULSE_WIDTH,      /* CFG PULSE_WIDTH_US=<float>         */
    CMD_CFG_ARMED,            /* CFG ARMED=<0|1>                    */
    CMD_CFG_ARM_TIMEOUT,      /* CFG ARM_TIMEOUT_MS=<int>           */
    CMD_CFG_CAM_XTR_SETUP,    /* CFG CAM_XTR_SETUP_US=<int>         */
    CMD_CFG_MIN_INTER_SHOT,   /* CFG MIN_INTER_SHOT_MS=<int>        */
    CMD_CFG_PRE_TRIGGER_DELAY,/* CFG PRE_TRIGGER_DELAY_MS=<int>     */
    CMD_CFG_DECAY_CONFIRM,    /* CFG DECAY_CONFIRM_MS=<int>         */
    CMD_CFG_STROBE_HOLD,      /* CFG STROBE_HOLD=<0|1> — sustain DIAG HIGH for calibration */
    CMD_CFG_STREAM_RMS,       /* CFG STREAM_RMS=<hz> — emit EVENT RMS at this rate; 0 = stop */
    CMD_CAM_PULSE,            /* CAM_PULSE <us>: drive cam2 XTR LOW for N microseconds */
    CMD_FIRE,                 /* FIRE                               */
    CMD_FIRE_PEAK,            /* FIRE_PEAK - fire train + return peak ADC0 over the
                               * train window. For LED current calibration sweeps
                               * (avoids Python timing slop). */
    CMD_STATUS,               /* STATUS                             */
    CMD_HEARTBEAT,            /* HEARTBEAT — keep-alive: refresh the arm deadline */
    CMD_RESET,                /* RESET                              */
    CMD_BOOTSEL,              /* BOOTSEL — reboot into BOOTSEL mode */
    CMD_SELFTEST,             /* SELFTEST — pulse non-strobe outputs + report */
    CMD_INVALID,              /* parse error — host gets back LOG line */
} pitrac_cmd_kind_t;

/* Parsed command + payload. Only the union member matching `kind` is valid.
 * No heap — interval lists go into the fixed-size float array. */
typedef struct {
    pitrac_cmd_kind_t kind;
    union {
        int32_t  threshold;
        uint32_t u32;                /* reused by ARM_TIMEOUT_MS / CAM_XTR_SETUP_US */
        bool     armed;
        float    pulse_width_us;
        struct {
            float    intervals_ms[STROBE_MAX_PULSES];
            uint8_t  count;
        } intervals;
    } u;
} pitrac_cmd_t;

/* --------------------------------------------------------- runtime state -- */

/* Live tunables read by DSP and strobe. Mutated only from the USB worker;
 * producers read-modify-write under a critical section. */
typedef struct {
    /* Detection */
    int32_t  mic_threshold;            /* raw RMS units */
    bool     armed;                    /* if false, DSP runs but never fires */

    /* Strobe */
    float    pulse_width_us;
    float    intervals_ms[STROBE_MAX_PULSES];
    uint8_t  interval_count;

    /* Arm keep-alive window. Each HEARTBEAT pushes the auto-disarm deadline
     * (owned by main.c's arm_gate, not stored here) out by this much; a
     * walked-away host drops the gate within arm_timeout_ms, so a forgotten
     * ARMED=1 can't hot-fire on noise. core1 writes (CFG ARM_TIMEOUT_MS),
     * core0 reads on (re)arm. Default 10000 (10 s). */
    uint32_t arm_timeout_ms;

    /* Delay between XTR LOW and strobe DMA start; tune up for slower sensors. */
    uint32_t cam_xtr_setup_us;

    /* Cooldown between fires (boost recharge + LED thermal). Host can tighten
     * for calibration sweeps. */
    uint32_t min_inter_shot_ms;

    /* Delay from FIRE received to first cam XTR assert. Matches Pi-side
     * kPuttingStrobeDelayMs — host "ball settle" pause without a userspace sleep. */
    uint32_t pre_trigger_delay_ms;

    /* Informational, reported by STATUS. */
    uint64_t last_event_us;
    int32_t  last_rms;
    uint32_t event_count;

    /* Capture DMA re-arms before exhausting its transfer count. Surfaced in
     * STATUS so the ~6h self-heal is observable; a climbing value is normal. */
    uint32_t dma_rearm_count;

    /* Latest FIRE_PEAK result. Written by core0 in strobe_fire_peak, read by
     * core1 for EVENT PEAK; the MAILBOX push is the memory barrier between them. */
    uint16_t last_peak_adc;
    uint32_t last_peak_samples;
} pitrac_state_t;

/* ------------------------------------------------------------- functions -- */

/* Parse one '\n'-terminated line into `out`. `line` is mutated (NULs inserted
 * at field boundaries) so the caller's buffer must be writable. On failure
 * sets out->kind = CMD_INVALID and returns false. */
bool proto_parse_line(char *line, pitrac_cmd_t *out);

/* Format STATUS into `buf`: single line, '\n'-terminated, NUL-terminated.
 * Returns bytes written excluding the trailing NUL. */
int  proto_format_status(const pitrac_state_t *st, char *buf, int buflen);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_PICO_PROTO_H */
