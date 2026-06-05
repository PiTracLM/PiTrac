/*
 * core1_usb.c — USB CDC command/event worker, runs on RP2040 core 1.
 *
 *   core 0 : DSP loop (impact detection), strobe firing
 *   core 1 : USB CDC line buffering, command parsing, event emission
 *
 * Keeping USB off core 0 stops TinyUSB/host scheduling latency from stalling
 * the DSP. Multicore FIFO is the IPC: core 0 pushes MAILBOX_STRIKE on impact,
 * core 1 emits the EVENT line.
 *
 * CDC delivers writes in arbitrary-sized chunks (one 12-byte write may arrive
 * as 12 single-byte chunks), so we accumulate until '\n' before dispatching.
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
#include "sensor.h"

/* Mutated by core 1 (CFG commands), read by core 0 (DSP loop, strobe path).
 * 32-bit aligned field access is atomic on Cortex-M0+, so scalars need no lock.
 * The intervals array is safe only because the host reconfigures between shots,
 * not mid-flight; if that changes, add a sequence counter and double-buffer. */
/* Zero-init only; real defaults come from g_state_runtime_init() (called before
 * stdio_init_all). A static partial-float-array initializer under `volatile`
 * trips a .data-layout quirk that corrupts adjacent symbols incl. TinyUSB state
 * — symptom: USB enumerates but CDC TX never delivers. */
volatile pitrac_state_t g_state;

void g_state_runtime_init(void) {
    g_state.mic_threshold        = DSP_DEFAULT_THRESHOLD;
    g_state.armed                = false;
    g_state.pulse_width_us       = STROBE_DEFAULT_PULSE_WIDTH_US;
    g_state.interval_count       = STROBE_DEFAULT_INTERVAL_COUNT;
    g_state.arm_timeout_ms       = 10000;
    g_state.cam_xtr_setup_us     = CAM_XTR_DEFAULT_SETUP_US;
    g_state.min_inter_shot_ms    = STROBE_DEFAULT_MIN_INTER_SHOT_MS;
    g_state.pre_trigger_delay_ms = STROBE_DEFAULT_PRE_TRIGGER_DELAY_MS;
    g_state.last_event_us        = 0;
    g_state.last_rms             = 0;
    g_state.event_count          = 0;
    g_state.dma_rearm_count      = 0;

    static const float defaults[] = STROBE_DEFAULT_INTERVALS_MS;
    const size_t n = sizeof(defaults) / sizeof(defaults[0]);
    for (size_t i = 0; i < STROBE_MAX_PULSES; ++i) {
        g_state.intervals_ms[i] = (i < n) ? defaults[i] : 0.0f;
    }
}

static void emit_log(const char *msg);

/* STATUS worst case: ~232-char prefix + STROBE_MAX_PULSES intervals at ~9 chars
 * ("-1000.00,") + '\n' + '\0'. 640 covers it so the CSV never truncates. */
#define STATUS_LINE_BUF_SIZE  640

/* VSYS via the on-board ÷3 divider on ADC3 / GPIO 29; ref 3.3 V, 12-bit ADC.
 * Returns mV. Pico W returns 0: GPIO 29 is owned by CYW43 and needs a CYW43
 * SPI read (follow-up). */
static uint32_t read_vsys_mv(void) {
#if PITRAC_LED_VIA_CYW43
    return 0;
#else
    /* ADC mux is shared with core 0's FIRE_PEAK sweep; back off rather than
     * reprogram mid-sweep — a transient vsys=0 beats corrupting current-sense. */
    if (!strobe_adc_acquire()) return 0;

    adc_select_input(ADC_VSYS_CHANNEL);
    /* Average 8: divider node is high-impedance, single reads jitter even with
     * SMPS in forced-PWM. */
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += adc_read();
    uint32_t avg = sum / 8u;                /* 0..4095 */

    strobe_adc_release();

    /* mV = avg × (3300/4096) × 3; integer, multiply first. 3 × 3300 = 9900. */
    return (avg * 9900u) / 4096u;
#endif
}

/* HIGH when USB 5 V present. On Pico W, GP24 is CYW43-owned (cyw43_arch_gpio_get). */
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
        "pre_trigger_delay_ms=%lu strobe_hold=%d "
        "event_count=%lu dma_rearm=%lu vsys_mv=%lu vbus=%d fw=%s board=%s intervals=",
        (int)st->armed, (long)st->mic_threshold, (double)st->pulse_width_us,
        (unsigned long)st->min_inter_shot_ms,
        (unsigned long)st->pre_trigger_delay_ms,
        (int)strobe_is_held(),
        (unsigned long)st->event_count,
        (unsigned long)st->dma_rearm_count,
        (unsigned long)read_vsys_mv(),
        (int)vbus_present(),
        PITRAC_PICO_FW_VERSION,
        PITRAC_BOARD_NAME);
    if (n < 0 || n >= buflen) return n;

    /* Reserve final two bytes for '\n' + '\0' so a tight buffer can't consume
     * the last slot mid-token and drop the newline. Makes the bound provably
     * safe even though callers size buf for the worst case. */
    const int csv_limit = buflen - 2;

    /* CSV matches the CFG PULSE_INTERVALS input format so STATUS → CFG
     * round-trips on the host. */
    for (uint8_t i = 0; i < st->interval_count && n < csv_limit; ++i) {
        int k = snprintf(buf + n, (size_t)(csv_limit - n),
                         (i == 0) ? "%.2f" : ",%.2f",
                         (double)st->intervals_ms[i]);
        if (k < 0) break;
        if (n + k >= csv_limit) break;   /* token wouldn't fit whole; stop clean */
        n += k;
    }
    /* Two bytes reserved above, so this always fits. */
    buf[n++] = '\n';
    buf[n]   = '\0';
    return n;
}

/* --- IO helpers --------------------------------------------------------- */

/* Append pulse-train (pulse_us + intervals csv) to a STRIKE/MANUAL_FIRE line so
 * the host can correlate the firing pattern with what the capture saw. */
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
    /* printf to CDC is safe: core 1 main loop, not interrupt context. */
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
static int64_t fw_current_rms(void)   { return sensors_max_level(); }

/* Sentinels let CMD_CFG_INTERVALS and CMD_CFG_PULSE_WIDTH share one slot:
 * intervals_ms=NULL keeps current intervals, pulse_width_us=0 keeps current width. */
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
    /* core 0 owns the arm gate and the deadline; we only post intent. Blocking
     * push is fine — control, not droppable telemetry, and core 0 drains within
     * one bounded fire at worst. core 0 derives the deadline from arm_timeout_ms,
     * keeping it single-writer. */
    multicore_fifo_push_blocking(armed ? MAILBOX_REQ_ARM : MAILBOX_REQ_DISARM);
}

/* Keep-alive; droppable. A missed ping is harmless (next one refreshes inside
 * arm_timeout_ms), and it must never wedge core 1 behind a full FIFO. core 0
 * refreshes the deadline, only while armed. */
static void fw_heartbeat(void) {
    multicore_fifo_push_timeout_us(MAILBOX_REQ_HEARTBEAT, FIFO_PUSH_TIMEOUT_US);
}

static void fw_set_arm_timeout(uint32_t ms)        { g_state.arm_timeout_ms      = ms; }
static void fw_set_cam_xtr_setup(uint32_t us)      { g_state.cam_xtr_setup_us    = us; }
static void fw_set_min_inter_shot(uint32_t ms)     { g_state.min_inter_shot_ms   = ms; }
static void fw_set_pre_trigger_delay(uint32_t ms)  { g_state.pre_trigger_delay_ms = ms; }

static bool fw_hold_assert(void)  { return strobe_hold_assert(); }
static void fw_hold_release(void) { strobe_hold_release(); }

/* Best-effort: if core 0 is mid-train and the FIFO is full, drop rather than
 * wedge (host can re-issue FIRE). Unlike arm/disarm, a missed manual fire
 * leaves no lingering state. */
static void fw_request_manual_fire(void) {
    if (!multicore_fifo_push_timeout_us(MAILBOX_MANUAL_FIRE, FIFO_PUSH_TIMEOUT_US)) {
        emit_log("error: fire dropped (core busy, retry)");
    }
}

static void fw_request_fire_peak(void) {
    if (!multicore_fifo_push_timeout_us(MAILBOX_MANUAL_FIRE_PEAK, FIFO_PUSH_TIMEOUT_US)) {
        emit_log("error: fire_peak dropped (core busy, retry)");
    }
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

static uint32_t s_stream_rms_hz = 0;
static uint64_t s_next_rms_emit = 0;

static void fw_set_stream_rms_hz(uint32_t hz) {
    s_stream_rms_hz = hz;
    s_next_rms_emit = (hz > 0) ? time_us_64() + (1000000ULL / hz) : 0;
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
    snap.dma_rearm_count      = g_state.dma_rearm_count;

    char buf[STATUS_LINE_BUF_SIZE];
    proto_format_status(&snap, buf, sizeof(buf));
    fputs(buf, stdout);
}

static void fw_selftest(void) {
    /* Deliberately never pulses PIN_STROBE_OUT — even a microsecond flashes
     * the IR bank, the foot-gun this command avoids. */
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "SELFTEST vsys_mv=%lu vbus=%d armed=%d fw=%s",
        (unsigned long)read_vsys_mv(),
        (int)vbus_present(),
        (int)g_state.armed,
        PITRAC_PICO_FW_VERSION);
    if (n > 0 && n < (int)sizeof(buf)) {
        sensors_selftest_append(buf, (int)sizeof(buf), &n);
    }
    if (n > 0 && n < (int)sizeof(buf) - 1) {
        buf[n++] = '\n';
        buf[n]   = '\0';
    }
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
    .heartbeat             = fw_heartbeat,
    .cam_pulse             = fw_cam_pulse,
    .emit_log              = emit_log,
    .emit_status           = fw_emit_status,
    .selftest              = fw_selftest,
};

/* --- core 1 entry ------------------------------------------------------- */

void core1_usb_entry(void) {
    /* One max-length protocol line, no heap. */
    static char  line_buf[USB_CDC_LINE_BUFFER_SIZE];
    static uint16_t line_len = 0;
    static bool line_overflow_logged = false;

    /* stdio_usb_connected() reports DTR — our proxy for "host attending".
     * Tracked so we can safety-disarm if the host vanishes mid-armed. */
    bool was_connected = false;

    /* BOOT announce lives in main.c — fires before core 1 spins up, so the host
     * sees BOOT even if core 1 fails to start. */

    printf("LOG core1 up\n");

    while (true) {
        /* If the host dropped while armed, disarm so we don't fire on the next
         * sound with the user thinking we're off. Edge-detect: log once. */
        bool connected_now = stdio_usb_connected();
        if (was_connected && !connected_now) {
            if (g_state.armed) {
                /* core 0 is sole writer of armed — post disarm, don't clear here. */
                multicore_fifo_push_blocking(MAILBOX_REQ_DISARM);
                emit_log("armed=0 (USB disconnect)");
            }
        }
        was_connected = connected_now;

        /* Drain core-0 events first. FIFO is 8 deep, shouldn't back up; loop anyway. */
        while (multicore_fifo_rvalid()) {
            uint32_t evt = multicore_fifo_pop_blocking();
            uint64_t now = to_us_since_boot(get_absolute_time());

            switch (evt) {
            case MAILBOX_STRIKE:
                /* core 0 owns last_event_us; we only bump the counter + emit. */
                g_state.event_count++;
                emit_event_strike(now, g_state.last_rms);
                break;
            case MAILBOX_MANUAL_FIRE_DONE:
                /* core 0 owns last_event_us; we only bump the counter + emit. */
                g_state.event_count++;
                emit_event_manual(now);
                break;
            case MAILBOX_HARDWARE_FIRE_DONE:
                /* GP9 rising edge fired the strobe. */
                g_state.event_count++;
                emit_event_hardware(now);
                break;
            case MAILBOX_MANUAL_FIRE_PEAK_DONE: {
                /* core 0 fired a train and sampled ADC0; report peak so the
                 * Pi-side calibration sweep can pick a DAC. */
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
            case MAILBOX_RING_OVERFLOW:
                emit_log("warn: i2s ring overrun, dropped stale audio");
                break;
            default:
                break;
            }
        }

        if (s_stream_rms_hz > 0) {
            uint64_t now_us = time_us_64();
            if (now_us >= s_next_rms_emit) {
                /* Droppable telemetry. A host that stops reading with DTR still
                 * asserted would block printf on CDC backpressure and stall the
                 * whole loop (command parse + FIFO drain). Emit only while
                 * attached; schedule still advances so we don't burst on reconnect. */
                if (stdio_usb_connected()) {
                    int64_t rms = impact_detect_take_peak_rms();  /* peak since last emit, not instantaneous */
                    printf("EVENT RMS value=%lld timestamp=%llu\n",
                           (long long)rms,
                           (unsigned long long)now_us);
                }
                s_next_rms_emit = now_us + (1000000ULL / s_stream_rms_hz);
            }
        }

        /* Non-blocking CDC read; PICO_ERROR_TIMEOUT if nothing ready. */
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) {
            /* 1 ms back-off so we don't spin at 100%; latency stays imperceptible. */
            sleep_us(1000);
            continue;
        }
        if (ch < 0) continue;

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
