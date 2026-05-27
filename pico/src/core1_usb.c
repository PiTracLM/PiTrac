/*
 * core1_usb.c — USB CDC command/event worker, runs on RP2040 core 1.
 *
 * Split between cores:
 *   core 0 : DSP loop (impact detection), strobe firing
 *   core 1 : USB CDC: line buffering, command parsing, event emission
 *
 * The split is the obvious one: USB has unpredictable latency (TinyUSB
 * tasks, host scheduling, etc.) and we never want it to stall the DSP.
 * Multicore FIFO is the IPC: core 0 pushes MAILBOX_STRIKE on impact and
 * core 1 emits the EVENT line.
 *
 * Why one entry per line? USB CDC delivers data in arbitrary-sized chunks
 * — a host that does `write("CFG ARMED=1\n")` might be delivered as 1
 * chunk of 12 bytes, OR 12 chunks of 1 byte, depending on TinyUSB
 * buffering. We accumulate into a line buffer until we see '\n' and only
 * then dispatch. This makes the parser dead simple and tolerant of any
 * write granularity.
 */

#include "core1_usb.h"

#include <stdio.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"

#include "cmd_dispatcher.h"
#include "proto_parser.h"
#include "hardware/watchdog.h"

#include "config.h"
#include "led.h"
#include "proto.h"
#include "strobe.h"
#include "impact_detect.h"

/* --- shared runtime state, exposed to core 0 ----------------------------- */

/* Note on concurrency: this struct is mutated from core 1 (in response to
 * CFG commands) and read from core 0 (in the DSP loop and strobe path).
 * Reads/writes of 32-bit aligned fields are atomic on Cortex-M0+, so no
 * lock is needed for individual scalars. For the intervals array we get
 * away with it because changes only happen between shots — the host won't
 * reconfigure mid-flight. If that ever becomes a thing, add a sequence
 * counter and double-buffer. */
/* Zero-init only — the real defaults are filled in by g_state_runtime_init()
 * called from main() before stdio_init_all. A static initializer with a
 * partial float-array (intervals_ms is float[32] but the default has 8
 * entries) under `volatile` triggers a .data-section layout quirk that
 * corrupts adjacent symbols including TinyUSB internal state — symptom:
 * USB enumerates but CDC TX never delivers. */
volatile pitrac_state_t g_state;

void g_state_runtime_init(void) {
    g_state.mic_threshold        = DSP_DEFAULT_THRESHOLD;
    g_state.armed                = false;
    g_state.pulse_width_us       = STROBE_DEFAULT_PULSE_WIDTH_US;
    g_state.interval_count       = STROBE_DEFAULT_INTERVAL_COUNT;
    g_state.arm_deadline_us      = 0;
    g_state.arm_timeout_ms       = 10000;
    g_state.cam_xtr_setup_us     = CAM_XTR_DEFAULT_SETUP_US;
    g_state.min_inter_shot_ms    = STROBE_DEFAULT_MIN_INTER_SHOT_MS;
    g_state.pre_trigger_delay_ms = STROBE_DEFAULT_PRE_TRIGGER_DELAY_MS;
    g_state.last_event_us        = 0;
    g_state.last_rms             = 0;
    g_state.event_count          = 0;

    static const float defaults[] = STROBE_DEFAULT_INTERVALS_MS;
    const size_t n = sizeof(defaults) / sizeof(defaults[0]);
    for (size_t i = 0; i < STROBE_MAX_PULSES; ++i) {
        g_state.intervals_ms[i] = (i < n) ? defaults[i] : 0.0f;
    }
}

/* Forward decl — defined further down with the rest of the IO helpers. */
static void emit_log(const char *msg);

/* Read VSYS via the on-board ÷3 divider on ADC3 / GPIO 29. The Pico's
 * reference is 3.3 V; ADC is 12-bit (4096 codes). Multiply by 3 to recover
 * the real supply voltage. Returns millivolts.
 *
 * On Pico W, GPIO 29 is owned by the CYW43 chip — we'd need to take the
 * CYW43 SPI bus and read via a special API. For now we just return 0 on
 * Pico W; surfacing via a real CYW43 read is a follow-up. */
static uint32_t read_vsys_mv(void) {
#if PITRAC_LED_VIA_CYW43
    return 0;     /* Pico W — VSYS sense path not yet wired through CYW43. */
#else
    adc_select_input(ADC_VSYS_CHANNEL);
    /* Average a few samples — the divider node is high-impedance and a
     * single read can be jittery even with SMPS in forced-PWM. */
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += adc_read();
    uint32_t avg = sum / 8u;                /* 0..4095 */
    /* mV = avg × (3300 / 4096) × 3.  Integer math, no float; multiply first
     * to keep precision, then divide. 3 × 3300 = 9900. */
    return (avg * 9900u) / 4096u;
#endif
}

/* VBUS sense — HIGH when USB 5 V is present. On Pico W, GP24 is owned by
 * the CYW43 chip and the VBUS path goes through `cyw43_arch_gpio_get`. */
static bool vbus_present(void) {
#if PITRAC_LED_VIA_CYW43
    return cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN);
#else
    return gpio_get(PIN_VBUS_SENSE) != 0;
#endif
}

int proto_format_status(const pitrac_state_t *st, char *buf, int buflen) {
    int n = snprintf(buf, buflen,
        "STATUS armed=%d threshold=%ld pulse_us=%.2f min_inter_shot_ms=%lu "
        "pre_trigger_delay_ms=%lu decay_confirm_ms=%lu strobe_hold=%d "
        "vsys_mv=%lu vbus=%d intervals=",
        (int)st->armed, (long)st->mic_threshold, (double)st->pulse_width_us,
        (unsigned long)st->min_inter_shot_ms,
        (unsigned long)st->pre_trigger_delay_ms,
        (unsigned long)impact_detect_get_decay_confirm_ms(),
        (int)strobe_is_held(),
        (unsigned long)read_vsys_mv(),
        (int)vbus_present());
    if (n < 0 || n >= buflen) return n;

    /* Append comma-separated intervals — keep CSV in line with the
     * CFG PULSE_INTERVALS input format so round-tripping STATUS → CFG is
     * trivial from the host side. */
    for (uint8_t i = 0; i < st->interval_count && n < buflen - 16; ++i) {
        int k = snprintf(buf + n, buflen - n,
                         (i == 0) ? "%.2f" : ",%.2f",
                         (double)st->intervals_ms[i]);
        if (k < 0) break;
        n += k;
    }
    if (n < buflen - 2) {
        buf[n++] = '\n';
        buf[n]   = '\0';
    }
    return n;
}

/* --- IO helpers --------------------------------------------------------- */

/* Append the pulse-train description (pulse_us + intervals csv) to a STRIKE
 * or MANUAL_FIRE event line. Lets the host correlate the firing pattern
 * with whatever cameras / capture pipeline saw downstream. */
static void emit_pattern_suffix(void) {
    printf(" pulse_us=%.3f intervals=", (double)g_state.pulse_width_us);
    uint8_t n = g_state.interval_count;
    if (n > STROBE_MAX_PULSES) n = STROBE_MAX_PULSES;
    for (uint8_t i = 0; i < n; ++i) {
        printf((i == 0) ? "%.3f" : ",%.3f", (double)g_state.intervals_ms[i]);
    }
    putchar('\n');
}

static void emit_event_strike(uint64_t ts_us, int32_t rms) {
    /* USB CDC stdout. printf is safe here because we're in a normal context
     * (core 1 main loop), not in an interrupt. */
    printf("EVENT STRIKE timestamp=%llu rms=%ld",
           (unsigned long long)ts_us, (long)rms);
    emit_pattern_suffix();
}

static void emit_event_manual(uint64_t ts_us) {
    printf("EVENT MANUAL_FIRE timestamp=%llu", (unsigned long long)ts_us);
    emit_pattern_suffix();
}

static void emit_event_hardware(uint64_t ts_us) {
    printf("EVENT HARDWARE_FIRE timestamp=%llu", (unsigned long long)ts_us);
    emit_pattern_suffix();
}

static void emit_log(const char *msg) {
    printf("LOG %s\n", msg);
}

static void fw_set_threshold(int32_t v) {
    g_state.mic_threshold = v;
    impact_detect_set_threshold(v);
}

static int32_t fw_get_threshold(void) { return g_state.mic_threshold; }
static int32_t fw_current_rms(void)   { return impact_detect_current_rms(); }

static void fw_set_decay_confirm(uint32_t ms) {
    impact_detect_set_decay_confirm_ms(ms);
}

/* Sentinels: intervals_ms=NULL keeps current intervals, pulse_width_us=0
 * keeps current width. Lets CMD_CFG_INTERVALS and CMD_CFG_PULSE_WIDTH share
 * one slot. */
static bool fw_set_pulse_train(const float *intervals_ms,
                               uint8_t count,
                               float pulse_width_us) {
    float pw = (pulse_width_us > 0.0f) ? pulse_width_us : g_state.pulse_width_us;
    if (count == 0) count = g_state.interval_count;

    float local_intervals[STROBE_MAX_PULSES];
    const float *src = intervals_ms;
    if (src == NULL) {
        for (uint8_t i = 0; i < count && i < STROBE_MAX_PULSES; ++i) {
            local_intervals[i] = g_state.intervals_ms[i];
        }
        src = local_intervals;
    }

    if (!strobe_set_pulse_train(src, count, pw)) return false;

    if (intervals_ms != NULL) {
        for (uint8_t i = 0; i < count; ++i) {
            g_state.intervals_ms[i] = intervals_ms[i];
        }
        g_state.interval_count = count;
    }
    if (pulse_width_us > 0.0f) {
        g_state.pulse_width_us = pulse_width_us;
    }
    return true;
}

static void fw_set_armed(bool armed) {
    if (armed) {
        g_state.arm_deadline_us =
            time_us_64() + (uint64_t)g_state.arm_timeout_ms * 1000u;
    }
    g_state.armed = armed;
}

static void fw_set_arm_timeout(uint32_t ms)        { g_state.arm_timeout_ms      = ms; }
static void fw_set_cam_xtr_setup(uint32_t us)      { g_state.cam_xtr_setup_us    = us; }
static void fw_set_min_inter_shot(uint32_t ms)     { g_state.min_inter_shot_ms   = ms; }
static void fw_set_pre_trigger_delay(uint32_t ms)  { g_state.pre_trigger_delay_ms = ms; }

static bool fw_hold_assert(void)  { return strobe_hold_assert(); }
static void fw_hold_release(void) { strobe_hold_release(); }

static void fw_request_manual_fire(void) {
    multicore_fifo_push_blocking(MAILBOX_MANUAL_FIRE);
}

static void fw_request_fire_peak(void) {
    multicore_fifo_push_blocking(MAILBOX_MANUAL_FIRE_PEAK);
}

static void fw_request_reset(void) {
    fflush(stdout);
    watchdog_reboot(0, 0, 100);
}

static void fw_request_bootsel(void) {
    fflush(stdout);
    sleep_ms(100);
    reset_usb_boot(0, 0);
}

static void fw_cam_pulse(uint32_t us) {
    strobe_cam_pulse(us);
}

/* Continuous mic-RMS emission. The variable is written and read only on
 * core 1 (CFG commands arrive here, the emit loop runs here), so no atomic
 * is needed. Zero means streaming is off; positive values are Hz. */
static uint32_t s_stream_rms_hz   = 0;
static uint64_t s_next_rms_emit   = 0;

static void fw_set_stream_rms_hz(uint32_t hz) {
    s_stream_rms_hz = hz;
    /* Send the first sample one period after now, not immediately, so a
     * frantic CFG STREAM_RMS=hz spam doesn't flood the wire. */
    if (hz > 0) {
        s_next_rms_emit = time_us_64() + (1000000ULL / hz);
    } else {
        s_next_rms_emit = 0;
    }
}

static void fw_emit_status(void) {
    pitrac_state_t snap;
    snap.mic_threshold        = g_state.mic_threshold;
    snap.armed                = g_state.armed;
    snap.pulse_width_us       = g_state.pulse_width_us;
    snap.interval_count       = g_state.interval_count;
    for (uint8_t i = 0; i < STROBE_MAX_PULSES; ++i) {
        snap.intervals_ms[i]  = g_state.intervals_ms[i];
    }
    snap.min_inter_shot_ms    = g_state.min_inter_shot_ms;
    snap.pre_trigger_delay_ms = g_state.pre_trigger_delay_ms;
    snap.last_event_us        = g_state.last_event_us;
    snap.last_rms             = g_state.last_rms;
    snap.event_count          = g_state.event_count;

    char buf[256];
    proto_format_status(&snap, buf, sizeof(buf));
    fputs(buf, stdout);
}

static void fw_selftest(void) {
    /* Deliberately does NOT pulse PIN_STROBE_OUT: a microsecond on that pin
     * flashes the IR bank, which is the foot-gun this command avoids. */
    char buf[160];
    snprintf(buf, sizeof(buf),
        "SELFTEST vsys_mv=%lu vbus=%d mic_rms=%ld armed=%d fw=%s\n",
        (unsigned long)read_vsys_mv(),
        (int)vbus_present(),
        (long)impact_detect_current_rms(),
        (int)g_state.armed,
        PITRAC_PICO_FW_VERSION);
    fputs(buf, stdout);

    for (int i = 0; i < 3; i++) {
        led_set(true);  sleep_ms(50);
        led_set(false); sleep_ms(50);
    }
    gpio_put(PIN_CAM2_XTR, 0);
    gpio_put(PIN_CAM1_XTR, 0);
    sleep_ms(10);
    gpio_put(PIN_CAM2_XTR, 1);
    gpio_put(PIN_CAM1_XTR, 1);
    emit_log("selftest complete (strobe pin not pulsed by design)");
}

static const hw_driver_t fw_driver = {
    .set_threshold         = fw_set_threshold,
    .get_threshold         = fw_get_threshold,
    .current_rms           = fw_current_rms,
    .set_decay_confirm     = fw_set_decay_confirm,
    .set_pulse_train       = fw_set_pulse_train,
    .set_armed             = fw_set_armed,
    .set_arm_timeout       = fw_set_arm_timeout,
    .set_cam_xtr_setup     = fw_set_cam_xtr_setup,
    .set_min_inter_shot    = fw_set_min_inter_shot,
    .set_pre_trigger_delay = fw_set_pre_trigger_delay,
    .hold_assert           = fw_hold_assert,
    .hold_release          = fw_hold_release,
    .set_stream_rms_hz     = fw_set_stream_rms_hz,
    .request_manual_fire   = fw_request_manual_fire,
    .request_fire_peak     = fw_request_fire_peak,
    .request_reset         = fw_request_reset,
    .request_bootsel       = fw_request_bootsel,
    .cam_pulse             = fw_cam_pulse,
    .emit_log              = emit_log,
    .emit_status           = fw_emit_status,
    .selftest              = fw_selftest,
};

/* --- core 1 entry ------------------------------------------------------- */

void core1_usb_entry(void) {
    /* Line accumulator: one max-length protocol line, no heap. */
    static char  line_buf[USB_CDC_LINE_BUFFER_SIZE];
    static uint16_t line_len = 0;
    static bool line_overflow_logged = false;

    /* Track USB CDC connection state so we can safety-disarm when the host
     * goes away mid-armed. stdio_usb_connected() returns whether DTR is
     * asserted — that's our proxy for "the host is paying attention". */
    bool was_connected = false;

    /* BOOT announce now lives in main.c (single source of truth, fires before
     * core 1 even spins up). Keeping that responsibility there means the host
     * still sees a BOOT line even if core 1 fails to start. */

    /* Announce core 1 startup. */
    printf("LOG core1 up\n");

    while (true) {
        /* USB-disconnect safety: if the host was driving us armed and dropped
         * the line, disarm so we don't sit there waiting to fire on the next
         * sound while the user thinks the system is off. Edge-detect so we
         * only log once per disconnect. */
        bool connected_now = stdio_usb_connected();
        if (was_connected && !connected_now) {
            if (g_state.armed) {
                g_state.armed = false;
                emit_log("armed=0 (USB disconnect)");
            }
        }
        was_connected = connected_now;

        /* Drain any pending events from core 0 first. The multicore FIFO is
         * 8 entries deep — should never back up under normal load, but we
         * loop just in case. */
        while (multicore_fifo_rvalid()) {
            uint32_t evt = multicore_fifo_pop_blocking();
            uint64_t now = to_us_since_boot(get_absolute_time());

            switch (evt) {
            case MAILBOX_STRIKE:
                /* core 0 already stamped last_event_us; we just bump the
                 * event counter and emit the wire line. Dual-writer race
                 * avoided by leaving last_event_us to a single core. */
                g_state.event_count++;
                emit_event_strike(now, g_state.last_rms);
                break;
            case MAILBOX_MANUAL_FIRE_DONE:
                /* core 0 fired a manual shot and is reporting back so we
                 * can emit the EVENT line. Same single-writer discipline:
                 * core 0 owns last_event_us. */
                g_state.event_count++;
                emit_event_manual(now);
                break;
            case MAILBOX_HARDWARE_FIRE_DONE:
                /* GP9 rising edge fired the strobe; emit the EVENT line. */
                g_state.event_count++;
                emit_event_hardware(now);
                break;
            case MAILBOX_MANUAL_FIRE_PEAK_DONE: {
                /* core 0 fired a strobe train and sampled ADC0; report the
                 * peak so the Pi-side calibration sweep can pick a DAC. */
                g_state.event_count++;
                char buf[96];
                snprintf(buf, sizeof(buf),
                         "EVENT PEAK timestamp=%llu adc=%u samples=%u\n",
                         (unsigned long long)now,
                         (unsigned)g_state.last_peak_adc,
                         (unsigned)g_state.last_peak_samples);
                fputs(buf, stdout);
                break;
            }
            case MAILBOX_DISARM_AFTER_FIRE:
                emit_log("armed=0 (auto-disarm after fire)");
                break;
            case MAILBOX_DISARM_TIMEOUT:
                emit_log("armed=0 (arm_timeout)");
                break;
            case MAILBOX_FIRE_REFUSED_COOLDOWN:
                emit_log("error: fire refused (cooldown not elapsed)");
                break;
            case MAILBOX_FIRE_REFUSED_HELD:
                emit_log("error: fire refused (strobe held for calibration)");
                break;
            default:
                break;
            }
        }

        /* Periodic RMS stream emission. When the host sets STREAM_RMS=hz,
         * we sample the energy estimate at that rate so the web UI can draw
         * a moving mic-level chart. Reading impact_detect_current_rms() from
         * core 1 is safe per the comment in impact_detect.c (torn read is
         * harmless for a level display). */
        if (s_stream_rms_hz > 0) {
            uint64_t now_us = time_us_64();
            if (now_us >= s_next_rms_emit) {
                int32_t rms = impact_detect_current_rms();
                printf("EVENT RMS value=%ld timestamp=%llu\n",
                       (long)rms,
                       (unsigned long long)now_us);
                s_next_rms_emit = now_us + (1000000ULL / s_stream_rms_hz);
            }
        }

        /* Pull a byte from USB CDC, non-blocking. getchar_timeout_us(0)
         * returns PICO_ERROR_TIMEOUT immediately if nothing's ready. */
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) {
            /* Nothing waiting. Brief sleep so we don't spin at 100% — keeps
             * the silicon cooler and the USB DMA happier. 1 ms is short
             * enough that command latency stays imperceptible. */
            sleep_us(1000);
            continue;
        }
        if (ch < 0) continue;

        /* On newline, terminate and dispatch. */
        if (ch == '\n' || ch == '\r') {
            if (line_len == 0) continue;  /* skip empty lines */
            line_buf[line_len] = '\0';
            pitrac_cmd_t cmd;
            proto_parse_line(line_buf, &cmd);
            cmd_dispatcher_apply(&cmd, &fw_driver);
            line_len = 0;
            line_overflow_logged = false;
            continue;
        }

        if (line_len < sizeof(line_buf) - 1u) {
            line_buf[line_len++] = (char)ch;
        } else if (!line_overflow_logged) {
            emit_log("error: line too long, truncating");
            line_overflow_logged = true;
        }
    }
}
