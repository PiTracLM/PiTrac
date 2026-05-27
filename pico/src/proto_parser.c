#include "proto_parser.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"   /* STROBE_MAX_PULSES, STROBE_MAX_PULSE_WIDTH_US, etc. */

/* Strip CR/LF/whitespace from both ends of `s` (in-place). Returns the
 * trimmed start pointer; the trailing trim adjusts the NUL. */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) --end;
    *end = '\0';
    return s;
}

/* Prefix-match a command keyword, requiring the next char to be a hard
 * terminator (NUL / space / tab). This prevents "FIREWORKS" from matching
 * "FIRE" : older strncmp(line, "FIRE", 4) accepted any trailing chars. */
static bool match_cmd(const char *line, const char *cmd) {
    size_t n = strlen(cmd);
    if (strncmp(line, cmd, n) != 0) return false;
    char next = line[n];
    return next == '\0' || next == ' ' || next == '\t';
}

/* Safe integer parse: returns true and writes *out iff the entire string is
 * a valid decimal integer. Rejects empty strings, trailing garbage, and
 * out-of-range (returns ERANGE). */
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

/* Safe float parse: same contract as parse_int. Also rejects non-finite. */
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

    /* Quick keyword dispatch. We compare against fixed prefixes; arguments
     * follow after a space or '='. Strict terminator-checked via match_cmd. */

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

    /* CAM_PULSE <us>: drive cam2 XTR LOW for N microseconds. Parser is
     * permissive on range; strobe_cam_pulse clamps to 1..100000. */
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

    /* CFG sub-commands. Format: "CFG <KEY>=<value>" */
    if (match_cmd(line, "CFG")) {
        char *kv = line + 3;
        /* Skip the separator(s) between CFG and the key. */
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
            /* Bounds-check at parse time. The dispatcher emits the LOG line
             * for CMD_INVALID; the parser stays free of side effects. */
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
            out->kind = CMD_CFG_INTERVALS;
            uint8_t n = 0;
            char *tok = val;
            while (*tok && n < STROBE_MAX_PULSES) {
                char *next = strchr(tok, ',');
                if (next) *next = '\0';
                float v;
                if (!parse_float(tok, &v)) {
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
            /* Host can tighten this for calibration sweeps (single-pulse,
             * tiny energy per fire) but never below the boost-cap-protection
             * floor : even 0 from the host snaps up to
             * STROBE_MIN_INTER_SHOT_MS_FLOOR to prevent back-to-back fires
             * before the cap recharges. */
            long v;
            if (!parse_int(val, &v)) return false;
            if (v < (long)STROBE_MIN_INTER_SHOT_MS_FLOOR) {
                v = (long)STROBE_MIN_INTER_SHOT_MS_FLOOR;
            }
            if (v > 60000) v = 60000;     /* 1 minute ceiling : sanity bound */
            out->kind = CMD_CFG_MIN_INTER_SHOT;
            out->u.u32 = (uint32_t)v;
            return true;
        }
        if (strcmp(key, "PRE_TRIGGER_DELAY_MS") == 0) {
            /* Mirrors kPuttingStrobeDelayMs from the Pi-side C++. 0 disables
             * (default). Bounded to keep a typo from blocking strobe_fire for
             * minutes. */
            long v;
            if (!parse_int(val, &v)) return false;
            if (v < 0)     v = 0;
            if (v > 10000) v = 10000;     /* 10 s ceiling */
            out->kind = CMD_CFG_PRE_TRIGGER_DELAY;
            out->u.u32 = (uint32_t)v;
            return true;
        }
        if (strcmp(key, "DECAY_CONFIRM_MS") == 0) {
            /* How long after onset the impact-detector waits for sustained
             * energy before firing. Lower = more sensitive (transient claps
             * trigger), higher = more selective (rejects short clicks).
             * 40 ms was the original compile-time default; 5 ms is the new
             * runtime default. impact_detect_set_decay_confirm_ms clamps
             * to 1..200. */
            long v;
            if (!parse_int(val, &v)) return false;
            if (v < 1)   v = 1;
            if (v > 200) v = 200;
            out->kind = CMD_CFG_DECAY_CONFIRM;
            out->u.u32 = (uint32_t)v;
            return true;
        }
        if (strcmp(key, "STROBE_HOLD") == 0) {
            /* Sustain the strobe pin HIGH for LED-current calibration.
             * 1 = assert, 0 = release. The Pi-side calibration sweep uses
             * this to keep DIAG HIGH while it reads ADC; a 200 ms hardware
             * timeout in firmware auto-releases as a safety net. */
            long v;
            if (!parse_int(val, &v)) return false;
            out->kind = CMD_CFG_STROBE_HOLD;
            out->u.armed = (v != 0);  /* reuse the bool union member */
            return true;
        }
    }
    return false;
}
