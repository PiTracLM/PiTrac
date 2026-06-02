#include "proto_parser.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"   /* STROBE_MAX_PULSES, STROBE_MAX_PULSE_WIDTH_US, etc. */

/* Trim whitespace both ends in-place; returns the new start pointer. */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) --end;
    *end = '\0';
    return s;
}

/* Prefix-match a keyword, requiring a hard terminator (NUL/space/tab) next
 * so "FIREWORKS" can't match "FIRE". */
static bool match_cmd(const char *line, const char *cmd) {
    size_t n = strlen(cmd);
    if (strncmp(line, cmd, n) != 0) return false;
    char next = line[n];
    return next == '\0' || next == ' ' || next == '\t';
}

/* Strict decimal parse: true only if the whole string is a valid integer.
 * Rejects empty, trailing garbage, and out-of-range. */
static bool parse_int(const char *s, long *out) {
    if (!s || !*s) return false;
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0) return false;
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\0') return false;
    *out = v;
    return true;
}

/* Same contract as parse_int; also rejects non-finite. */
static bool parse_float(const char *s, float *out) {
    if (!s || !*s) return false;
    char *end = NULL;
    errno = 0;
    float v = strtof(s, &end);
    if (errno != 0 || !isfinite(v)) return false;
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\0') return false;
    *out = v;
    return true;
}

bool proto_parse_line(char *line, pitrac_cmd_t *out) {
    memset(out, 0, sizeof(*out));
    out->kind = CMD_INVALID;

    line = trim(line);
    if (*line == '\0') {
        out->kind = CMD_NONE;
        return false;
    }

    if (match_cmd(line, "FIRE_PEAK")) {
        out->kind = CMD_FIRE_PEAK;
        return true;
    }
    if (match_cmd(line, "FIRE")) {
        out->kind = CMD_FIRE;
        return true;
    }
    if (match_cmd(line, "STATUS")) {
        out->kind = CMD_STATUS;
        return true;
    }
    if (match_cmd(line, "HEARTBEAT")) {
        out->kind = CMD_HEARTBEAT;
        return true;
    }
    if (match_cmd(line, "RESET")) {
        out->kind = CMD_RESET;
        return true;
    }
    if (match_cmd(line, "BOOTSEL")) {
        out->kind = CMD_BOOTSEL;
        return true;
    }
    if (match_cmd(line, "SELFTEST")) {
        out->kind = CMD_SELFTEST;
        return true;
    }

    /* CAM_PULSE <us>: drive cam2 XTR LOW for N us. strobe_cam_pulse clamps to 1..100000. */
    if (match_cmd(line, "CAM_PULSE")) {
        char *arg = line + strlen("CAM_PULSE");
        while (*arg == ' ' || *arg == '\t') ++arg;
        if (*arg == '\0') return false;
        long v;
        if (!parse_int(arg, &v) || v < 0) return false;
        out->kind = CMD_CAM_PULSE;
        out->u.u32 = (uint32_t)v;
        return true;
    }

    /* CFG <KEY>=<value> */
    if (match_cmd(line, "CFG")) {
        char *kv = line + 3;
        while (*kv == ' ' || *kv == '\t') ++kv;
        char *eq = strchr(kv, '=');
        if (!eq) return false;
        *eq = '\0';
        const char *key = trim(kv);
        char *val = trim(eq + 1);

        if (strcmp(key, "MIC_THRESHOLD") == 0) {
            long v;
            if (!parse_int(val, &v)) return false;
            out->kind = CMD_CFG_THRESHOLD;
            out->u.threshold = (int32_t)v;
            return true;
        }
        if (strcmp(key, "ARMED") == 0) {
            long v;
            if (!parse_int(val, &v)) return false;
            out->kind = CMD_CFG_ARMED;
            out->u.armed = (v != 0);
            return true;
        }
        if (strcmp(key, "PULSE_WIDTH_US") == 0) {
            /* Bounds-check here; dispatcher logs CMD_INVALID, parser stays side-effect free. */
            float v;
            if (!parse_float(val, &v) || v <= 0.0f || v > STROBE_MAX_PULSE_WIDTH_US) {
                out->kind = CMD_INVALID;
                return false;
            }
            out->kind = CMD_CFG_PULSE_WIDTH;
            out->u.pulse_width_us = v;
            return true;
        }
        if (strcmp(key, "PULSE_INTERVALS") == 0) {
            /* Reject empty list: otherwise it's a false ACK and stale intervals stay applied. */
            if (*val == '\0') {
                out->kind = CMD_INVALID;
                return false;
            }
            out->kind = CMD_CFG_INTERVALS;
            uint8_t n = 0;
            char *tok = val;
            while (*tok) {
                /* Reject overflow, don't truncate — silent truncation is a false ACK. */
                if (n >= STROBE_MAX_PULSES) {
                    out->kind = CMD_INVALID;
                    return false;
                }
                char *next = strchr(tok, ',');
                if (next) *next = '\0';
                float v;
                if (!parse_float(tok, &v)) {
                    out->kind = CMD_INVALID;
                    return false;
                }
                /* Above STROBE_MAX_INTERVAL_MS would push DMA wait past the watchdog. */
                if (v < 0.0f || v > STROBE_MAX_INTERVAL_MS) {
                    out->kind = CMD_INVALID;
                    return false;
                }
                out->u.intervals.intervals_ms[n++] = v;
                if (!next) break;
                tok = next + 1;
            }
            out->u.intervals.count = n;
            return true;
        }
        if (strcmp(key, "ARM_TIMEOUT_MS") == 0) {
            long v;
            if (!parse_int(val, &v)) return false;
            if (v < 100) v = 100;
            if (v > 300000) v = 300000;
            out->kind = CMD_CFG_ARM_TIMEOUT;
            out->u.u32 = (uint32_t)v;
            return true;
        }
        if (strcmp(key, "CAM_XTR_SETUP_US") == 0) {
            long v;
            if (!parse_int(val, &v)) return false;
            if (v < 100) v = 100;
            if (v > 10000) v = 10000;
            out->kind = CMD_CFG_CAM_XTR_SETUP;
            out->u.u32 = (uint32_t)v;
            return true;
        }
        if (strcmp(key, "MIN_INTER_SHOT_MS") == 0) {
            /* Floored at STROBE_MIN_INTER_SHOT_MS_FLOOR so back-to-back fires
             * can't drain the boost cap before it recharges. */
            long v;
            if (!parse_int(val, &v)) return false;
            if (v < (long)STROBE_MIN_INTER_SHOT_MS_FLOOR) {
                v = (long)STROBE_MIN_INTER_SHOT_MS_FLOOR;
            }
            if (v > 60000) v = 60000;     /* 1 min ceiling */
            out->kind = CMD_CFG_MIN_INTER_SHOT;
            out->u.u32 = (uint32_t)v;
            return true;
        }
        if (strcmp(key, "PRE_TRIGGER_DELAY_MS") == 0) {
            /* Mirrors kPuttingStrobeDelayMs on the Pi side. 0 disables (default). */
            long v;
            if (!parse_int(val, &v)) return false;
            if (v < 0)     v = 0;
            if (v > 10000) v = 10000;     /* 10 s ceiling */
            out->kind = CMD_CFG_PRE_TRIGGER_DELAY;
            out->u.u32 = (uint32_t)v;
            return true;
        }
        if (strcmp(key, "DECAY_CONFIRM_MS") == 0) {
            /* Post-onset sustained-energy window before firing. Lower = more
             * sensitive, higher = rejects short clicks. Runtime default 5 ms
             * (was 40 ms compile-time); detector clamps to 1..200. */
            long v;
            if (!parse_int(val, &v)) return false;
            if (v < 1)   v = 1;
            if (v > 200) v = 200;
            out->kind = CMD_CFG_DECAY_CONFIRM;
            out->u.u32 = (uint32_t)v;
            return true;
        }
        if (strcmp(key, "STREAM_RMS") == 0) {
            /* Mic RMS emission rate (Hz). 0 stops; capped at STREAM_RMS_MAX_HZ
             * so a typo can't flood the USB CDC TX queue. */
            long v;
            if (!parse_int(val, &v)) return false;
            if (v < 0) v = 0;
            if (v > (long)STREAM_RMS_MAX_HZ) v = (long)STREAM_RMS_MAX_HZ;
            out->kind = CMD_CFG_STREAM_RMS;
            out->u.u32 = (uint32_t)v;
            return true;
        }
        if (strcmp(key, "STROBE_HOLD") == 0) {
            /* Hold strobe pin (DIAG) HIGH for LED-current calibration ADC reads.
             * 1=assert, 0=release; firmware 200 ms hardware timeout auto-releases. */
            long v;
            if (!parse_int(val, &v)) return false;
            out->kind = CMD_CFG_STROBE_HOLD;
            out->u.armed = (v != 0);  /* reuse bool union member */
            return true;
        }
    }
    return false;
}
