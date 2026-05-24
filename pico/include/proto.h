/*
 * proto.h — USB CDC command/event protocol shared between the Pi 5 host and
 * the Pico. The wire format is line-delimited ASCII; this header defines the
 * tokens and a small state struct that the parser fills in. We keep it
 * header-only and inline so a smoke test could include this header from the
 * host side too (e.g. a Python ctypes wrapper or a C utility on the Pi).
 *
 * Wire format rules:
 *   - One command per line, terminated by '\n'. '\r' is tolerated and stripped.
 *   - Commands are case-sensitive (KEEP IT SIMPLE — saves an inner loop).
 *   - Numbers parse as standard C strtof / strtol.
 *   - Lists are comma-separated, no spaces. Whitespace inside a value is undefined.
 *
 * Why text instead of binary? Two reasons:
 *   1. Debugging: `picocom /dev/ttyACM0 -b 115200` and you can drive it by hand.
 *   2. Zero parser surface area: no length prefixes to get wrong, no endianness.
 * USB CDC bandwidth is way more than enough — we send maybe a few hundred
 * bytes per shot, not megabytes.
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

/* Commands the host can send. The parser turns each line into one of these
 * enum values; the dispatcher in core1_usb.c switches on it. */
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
    CMD_CAM_PULSE,            /* CAM_PULSE <us>: drive cam2 XTR LOW for N microseconds */
    CMD_FIRE,                 /* FIRE                               */
    CMD_STATUS,               /* STATUS                             */
    CMD_RESET,                /* RESET                              */
    CMD_BOOTSEL,              /* BOOTSEL — reboot into BOOTSEL mode */
    CMD_SELFTEST,             /* SELFTEST — pulse non-strobe outputs + report */
    CMD_INVALID,              /* parse error — host gets back LOG line */
} pitrac_cmd_kind_t;

/* Parsed command + payload. Only one of the union members is valid; check
 * `kind`. We avoid heap allocation entirely — interval lists go into the
 * fixed-size float array. */
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

/* Mirror of the live tunables — read by the DSP and strobe modules. Mutated
 * only from the USB worker; the producers read-modify-write under a critical
 * section (cheap on M0+, single sram-mem-protect access). */
typedef struct {
    /* Detection */
    int32_t  mic_threshold;            /* raw RMS units */
    bool     armed;                    /* if false, DSP runs but never fires */

    /* Strobe */
    float    pulse_width_us;
    float    intervals_ms[STROBE_MAX_PULSES];
    uint8_t  interval_count;

    /* Auto-disarm: armed clears itself after either a fire or this timeout
     * expires. Keeps a forgotten ARMED=1 from quietly hot-firing on noise. */
    uint64_t arm_deadline_us;          /* absolute us at which to auto-disarm */
    uint32_t arm_timeout_ms;           /* default 10000 (10 s) */

    /* Camera XTR setup delay between asserting XTR LOW and starting the
     * strobe DMA. Tunable so we can compensate for slower sensors. */
    uint32_t cam_xtr_setup_us;

    /* Cooldown between consecutive fires. Boost-converter recharge + LED
     * thermal protection. Host can tighten this for calibration sweeps. */
    uint32_t min_inter_shot_ms;

    /* Delay between FIRE-received and first cam XTR assertion. Matches the
     * Pi-side kPuttingStrobeDelayMs — lets host insert a "ball settle"
     * pause without burning userspace sleep cycles. */
    uint32_t pre_trigger_delay_ms;

    /* Bookkeeping — purely informational, reported by STATUS */
    uint64_t last_event_us;
    int32_t  last_rms;
    uint32_t event_count;
} pitrac_state_t;

/* ------------------------------------------------------------- functions -- */

/* Parse one '\n'-terminated line into `out`. `line` is mutated (NUL inserted
 * at field boundaries) so the caller's buffer must be writable. Returns
 * true on a recognised command; on failure, fills out->kind = CMD_INVALID
 * and returns false. */
bool proto_parse_line(char *line, pitrac_cmd_t *out);

/* Format the runtime status into `buf` for emission as a STATUS reply.
 * Output is a single line (no embedded newlines), terminated by '\n', NUL
 * terminated. Returns bytes written excluding the trailing NUL. */
int  proto_format_status(const pitrac_state_t *st, char *buf, int buflen);

#ifdef __cplusplus
}
#endif

#endif /* PITRAC_PICO_PROTO_H */
