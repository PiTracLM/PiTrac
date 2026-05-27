/*
 * strobe.c — IR strobe pulse-train compiler + PIO/DMA driver.
 *
 * The compiled-pattern approach matters because we need sub-microsecond
 * latency from "fire decision" to first pulse rising edge. By pre-compiling
 * the waveform into a RAM buffer at config time, the fire path becomes:
 *
 *   strobe_fire()
 *     ├─ pull cam2 XTR low                            (~ns: GPIO write)
 *     ├─ busy-wait camera setup delay                 (~1 ms)
 *     ├─ dma_channel_set_read_addr / set_trans_count  (~ns)
 *     ├─ dma_channel_start                            (~ns)
 *     └─ PIO begins clocking pulses immediately
 *
 * Total CPU-side latency from impact detection to first PIO output edge is
 * dominated by the 1 ms camera setup; the strobe itself fires within a
 * handful of cycles of strobe_fire() being called.
 *
 * Pattern encoding (matches ir_strobe.pio):
 *   word[31..16] = low_count - 1   (cycles to hold pin LOW after the pulse)
 *   word[15..0]  = high_count - 1  (cycles to hold pin HIGH for the pulse)
 *
 * One pulse + following gap = one 32-bit word, IF the gap fits in 16 bits.
 * At 125 MHz PIO clock, 16-bit max = 65535 cycles = 524.28 µs. Anything
 * longer needs a follow-up word with high_count = 0 (zero-width "phantom"
 * pulse — but high_count must be >= 1 for the PIO loop to terminate, so we
 * encode "wait 1 cycle high (8 ns blip — invisible to the LED driver) then
 * wait another 524 µs low" and repeat).
 *
 * Final terminator: a word with high_count=1, low_count=1 guarantees the
 * pin ends LOW with the loop in a defined state ready for the next fire.
 */

#include "strobe.h"

#include <math.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/watchdog.h"

#include "ir_strobe.pio.h"
#include "proto.h"

/* Runtime state lives in core1_usb.c (one source of truth). We poke at the
 * cam_xtr_setup_us field on every fire. */
extern volatile pitrac_state_t g_state;

/* The compiled pulse pattern. Sized for worst case: each interval might
 * need multiple "wait" words if the gap exceeds 524 µs. We bound it at
 * 4× STROBE_MAX_PULSES + 2 (head pulse + 3 wait extensions + terminator). */
#define STROBE_PATTERN_MAX_WORDS  (STROBE_MAX_PULSES * 4 + 2)

static uint32_t s_pattern[STROBE_PATTERN_MAX_WORDS];
static uint32_t s_pattern_len = 0;

/* Runtime tunables — keep our own copy so strobe_get_*() can satisfy STATUS
 * queries without reaching across modules. */
static float    s_pulse_width_us = STROBE_DEFAULT_PULSE_WIDTH_US;
static float    s_intervals_ms[STROBE_MAX_PULSES];
static uint8_t  s_interval_count = 0;

/* --- Sustained-hold state ----------------------------------------------- */

/* When true, PIN_STROBE_OUT is owned by SIO (driven HIGH directly) instead
 * of by the PIO state machine. Used by the Pi-side calibration sweep to
 * hold the gate driver on long enough for an ADC reading. */
static volatile bool      s_hold_active   = false;
static volatile uint64_t  s_hold_deadline_us = 0;

/* --- Pattern compiler ------------------------------------------------------ */

/* Encode one (high_cycles, low_cycles) PIO word. Both values are in PIO
 * clock cycles. Caller must guarantee both fit in 16 bits unless extension
 * loops handle the overflow. */
static inline uint32_t encode_word(uint32_t high_cycles, uint32_t low_cycles) {
    /* Subtract 1 because the PIO `jmp y-- / x--` loops execute one extra
     * iteration before the counter wraps to all-ones and terminates. */
    uint32_t h = (high_cycles == 0) ? 0 : (high_cycles - 1);
    uint32_t l = (low_cycles  == 0) ? 0 : (low_cycles  - 1);
    /* OSR shift_right convention: low half pops first via `out y,16`, so
     * high_count occupies the low 16 bits of the word. */
    return ((l & 0xFFFFu) << 16) | (h & 0xFFFFu);
}

/* Push one (high, low) cycle pair into s_pattern, splitting `low` into
 * multiple words if it exceeds the 16-bit per-word maximum. Returns false
 * on buffer overflow. */
static bool append_pulse(uint32_t high_cycles, uint32_t low_cycles) {
    const uint32_t MAX16 = 0xFFFFu;

    if (s_pattern_len >= STROBE_PATTERN_MAX_WORDS) return false;
    /* First word carries the actual pulse + as much of the low time as fits. */
    uint32_t low_first = (low_cycles > MAX16) ? MAX16 : low_cycles;
    s_pattern[s_pattern_len++] = encode_word(high_cycles, low_first);
    low_cycles -= low_first;

    /* Continuation words: high_cycles = 1 (single PIO clock, ~8 ns, invisible
     * to the LED driver), plus another chunk of LOW time. */
    while (low_cycles > 0) {
        if (s_pattern_len >= STROBE_PATTERN_MAX_WORDS) return false;
        uint32_t chunk = (low_cycles > MAX16) ? MAX16 : low_cycles;
        s_pattern[s_pattern_len++] = encode_word(1u, chunk);
        low_cycles -= chunk;
    }
    return true;
}

/* --- Public API ------------------------------------------------------------ */

bool strobe_set_pulse_train(const float *intervals_ms,
                            uint8_t count,
                            float pulse_width_us) {
    if (count > STROBE_MAX_PULSES) return false;
    if (pulse_width_us <= 0.0f) return false;
    /* Pete: hard cap to keep the boost converter inside its sane operating
     * region — beyond ~500 µs the rail droops and the LED current sags. */
    if (!isfinite(pulse_width_us) || pulse_width_us > STROBE_MAX_PULSE_WIDTH_US) {
        return false;
    }
    /* Train energy cap: pulse_width × count must stay under
     * STROBE_MAX_TRAIN_ON_TIME_US so a host can't request a hot-enough
     * train to over-pulse the LED bank. Real shots use ≪ 100 µs total. */
    if ((float)count * pulse_width_us > STROBE_MAX_TRAIN_ON_TIME_US) {
        return false;
    }

    /* Stash configuration for STATUS reporting. */
    s_pulse_width_us = pulse_width_us;
    s_interval_count = count;
    for (uint8_t i = 0; i < count; ++i) s_intervals_ms[i] = intervals_ms[i];

    /* Compile to PIO words. PIO clock = sysclk (we set clkdiv = 1.0f).
     * 125 MHz → 1 cycle = 8 ns. */
    const float pio_hz = (float)clock_get_hz(clk_sys);
    const uint32_t high_cycles = (uint32_t)(pulse_width_us * 1e-6f * pio_hz);
    if (high_cycles < 2u) return false;  /* PIO needs ≥ 1; round-up safety */

    s_pattern_len = 0;

    /* Interval semantics — matches the Pi-side C++ pipeline (pulse_strobe.cpp
     * BuildPulseTrain): `intervals_ms[N]` is the LOW time AFTER pulse N (gap
     * only, not rising-edge-to-rising-edge period). Keeping the same
     * convention as the existing Pi code means host CFG vectors can be sent
     * verbatim without arithmetic conversion at the boundary.
     *
     * For each interval N:
     *   pulse HIGH for `pulse_width_us`
     *   pulse LOW  for `intervals_ms[N]`
     *
     * If the interval is 0 (terminator) we emit just the pulse with a tiny
     * tail and stop.
     */
    for (uint8_t i = 0; i < count; ++i) {
        float gap_ms = intervals_ms[i];

        /* Per-interval upper bound. Rejects pathological values that would
         * push DMA wait_for_finish past the watchdog timeout and reset the
         * Pico mid-train (PIO keeps clocking through a watchdog-light reset
         * and the LED would stay on). */
        if (gap_ms > STROBE_MAX_INTERVAL_MS) return false;

        if (gap_ms <= 0.0f) {
            /* Terminator interval — emit the pulse with no follow-on gap and
             * break. (Or, if this is the very first interval, that's just an
             * empty train; user error but we handle gracefully.) */
            if (i == 0) break;
            if (!append_pulse(high_cycles, 4u)) return false;  /* 32 ns tail */
            break;
        }
        /* Gap-only: low_cycles is the full interval, not (interval - high). */
        uint32_t low_cycles = (uint32_t)(gap_ms * 1e-3f * pio_hz);
        if (low_cycles < 4u) low_cycles = 4u;     /* PIO underrun floor */
        if (!append_pulse(high_cycles, low_cycles)) return false;
    }

    /* Final terminator — guarantees the pin lands LOW with a defined PIO
     * state. Tiny HIGH blip (1 cycle = 8 ns, way under MOSFET turn-on time
     * so the LED never lights) followed by 1 cycle LOW. */
    if (s_pattern_len < STROBE_PATTERN_MAX_WORDS) {
        s_pattern[s_pattern_len++] = encode_word(1u, 1u);
    }
    return true;
}

bool strobe_init(void) {
    /* Cam2 XTR — straight GPIO, active low. Idle HIGH. */
    gpio_init(PIN_CAM2_XTR);
    gpio_set_dir(PIN_CAM2_XTR, GPIO_OUT);
    gpio_put(PIN_CAM2_XTR, 1);

    /* Cam1 XTR — same convention, idle HIGH. Reserved for future stereo
     * support; harmless on single-camera installs. */
    gpio_init(PIN_CAM1_XTR);
    gpio_set_dir(PIN_CAM1_XTR, GPIO_OUT);
    gpio_put(PIN_CAM1_XTR, 1);

    /* Strobe pin — pin pad floats between reset and PIO claiming the pin,
     * which can briefly trip the MOSFET gate. Drive it LOW explicitly before
     * handing it over so the LED can never light during boot. */
    gpio_init(PIN_STROBE_OUT);
    gpio_set_dir(PIN_STROBE_OUT, GPIO_OUT);
    gpio_put(PIN_STROBE_OUT, 0);

    /* Load the PIO program. Bail if there's not enough instruction memory. */
    if (!pio_can_add_program(STROBE_PIO, &ir_strobe_program)) {
        return false;
    }
    uint pio_offset = pio_add_program(STROBE_PIO, &ir_strobe_program);
    ir_strobe_program_init(STROBE_PIO, STROBE_PIO_SM, pio_offset,
                           PIN_STROBE_OUT, 1.0f /* clkdiv: sysclk-rate */);

    /* DMA channel: 32-bit transfers, increment read pointer, fixed write
     * pointer = PIO TX FIFO. DREQ = STROBE_PIO TX so DMA waits when the
     * FIFO is full. */
    dma_channel_config c = dma_channel_get_default_config(STROBE_DMA_CHAN);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(STROBE_PIO, STROBE_PIO_SM, true));

    /* Configure but don't start — we'll re-set read addr + count and trigger
     * on each strobe_fire(). */
    dma_channel_configure(
        STROBE_DMA_CHAN,
        &c,
        &STROBE_PIO->txf[STROBE_PIO_SM],  /* write to PIO TX FIFO */
        s_pattern,                         /* read from pattern buffer (placeholder) */
        0,                                 /* count: 0 = inactive */
        false                              /* don't start yet */
    );

    /* NOTE: the default pulse-train compile was moved out of strobe_init() to
     * after stdio_init_all() in main.c — pattern compilation does float math
     * (softfloat helpers on Cortex-M0+) and was implicated in a boot-time
     * TinyUSB state corruption that produced a silent CDC TX. Call
     * strobe_set_pulse_train() from main() once USB is up. */
    return true;
}

bool strobe_fire(void) {
    if (s_pattern_len == 0u) return false;

    /* Refuse to fire while the Pi is holding DIAG HIGH for calibration —
     * the pin is currently owned by SIO, not PIO, so the PIO state machine
     * can't drive it anyway. */
    if (s_hold_active) return false;

    /* Fire-time sanity check: each compiled word's high_count must fit within
     * the per-pulse-width budget. Stack corruption that pokes 0xFFFF into the
     * low 16 bits of a word would otherwise hold the LED on for 524 ms at
     * 125 MHz PIO clock — well past the boost cap's spec. Cheap defense-in-
     * depth on top of strobe_set_pulse_train's compile-time bounds. */
    const uint32_t pio_hz = (uint32_t)clock_get_hz(clk_sys);
    const uint32_t max_high =
        (uint32_t)(STROBE_MAX_PULSE_WIDTH_US * 1e-6f * (float)pio_hz);
    for (uint32_t i = 0; i < s_pattern_len; ++i) {
        if ((s_pattern[i] & 0xFFFFu) > max_high) return false;
    }

    /* Pre-trigger delay — host-tunable pause before we touch the camera. Used
     * by putts (kPuttingStrobeDelayMs equivalent). 0 by default = no-op. */
    if (g_state.pre_trigger_delay_ms > 0u) {
        sleep_ms(g_state.pre_trigger_delay_ms);
    }

    /* Step 1: open camera shutter (active low). Drive both cam triggers in
     * lockstep — single-camera installs ignore PIN_CAM1_XTR, dual-camera
     * stereo installs see synchronised exposure starts. */
    gpio_put(PIN_CAM2_XTR, 0);
    gpio_put(PIN_CAM1_XTR, 0);

    /* Step 2: tiny setup delay — IMX296/Mira220 spec'd at ~few hundred µs
     * shutter response. Default is CAM_XTR_DEFAULT_SETUP_US (1 ms); host
     * may have tuned it via `CFG CAM_XTR_SETUP_US=<int>`. busy_wait_us is
     * interrupt-safe and stalls under 1 instruction overhead. */
    busy_wait_us(g_state.cam_xtr_setup_us);

    /* Step 3: kick the DMA. Re-set read address + transfer count each time
     * because the channel doesn't auto-rewind. set_trans_count(true) starts
     * immediately. */
    dma_channel_set_read_addr(STROBE_DMA_CHAN, s_pattern, false);
    dma_channel_set_trans_count(STROBE_DMA_CHAN, s_pattern_len, true /* start */);

    /* The strobe is now firing autonomously. We could wait for completion
     * and then release XTR, but the train is short enough (max ~30 ms for
     * all 8 pulses with the longest gaps) that we just block here. Keeps
     * the API simple and the XTR timing predictable for the camera. */
    dma_channel_wait_for_finish_blocking(STROBE_DMA_CHAN);

    /* Step 4: release both XTR pins back high. */
    gpio_put(PIN_CAM2_XTR, 1);
    gpio_put(PIN_CAM1_XTR, 1);
    return true;
}

bool strobe_fire_peak(uint16_t *peak_adc_out, uint32_t *samples_out) {
    if (peak_adc_out)  *peak_adc_out = 0u;
    if (samples_out)   *samples_out  = 0u;

    /* On Pico W main.c keeps ADC inside the CYW43 guard, so we bring it
     * up the first time the host actually wants a peak read. */
    static bool s_adc_ready = false;
    if (!s_adc_ready) {
        adc_init();
        adc_gpio_init(PIN_CUR_SENSE_ADC);
        s_adc_ready = true;
    }
    adc_select_input(ADC_CUR_SENSE_CHANNEL);

    if (s_pattern_len == 0u) return false;
    if (s_hold_active) return false;

    const uint32_t pio_hz = (uint32_t)clock_get_hz(clk_sys);
    const uint32_t max_high =
        (uint32_t)(STROBE_MAX_PULSE_WIDTH_US * 1e-6f * (float)pio_hz);
    for (uint32_t i = 0; i < s_pattern_len; ++i) {
        if ((s_pattern[i] & 0xFFFFu) > max_high) return false;
    }

    if (g_state.pre_trigger_delay_ms > 0u) {
        sleep_ms(g_state.pre_trigger_delay_ms);
    }

    gpio_put(PIN_CAM2_XTR, 0);
    gpio_put(PIN_CAM1_XTR, 0);
    busy_wait_us(g_state.cam_xtr_setup_us);

    dma_channel_set_read_addr(STROBE_DMA_CHAN, s_pattern, false);
    dma_channel_set_trans_count(STROBE_DMA_CHAN, s_pattern_len, true);

    /* RP2040 ADC ~= 2 us/sample. With 8.68 us pulses ~4 samples land in any
     * given pulse-ON window; running max captures the peak. 60 ms deadline
     * keeps a wedged DMA from chewing through the 2 s watchdog. */
    const uint64_t adc_deadline = time_us_64() + 60000ull;
    uint16_t peak = 0u;
    uint32_t samples = 0u;
    while (dma_channel_is_busy(STROBE_DMA_CHAN) && time_us_64() < adc_deadline) {
        uint16_t v = adc_read();
        if (v > peak) peak = v;
        ++samples;
        if ((samples & 0x3FFu) == 0u) watchdog_update();
    }
    watchdog_update();

    gpio_put(PIN_CAM2_XTR, 1);
    gpio_put(PIN_CAM1_XTR, 1);

    if (peak_adc_out) *peak_adc_out = peak;
    if (samples_out)  *samples_out  = samples;
    return true;
}

void strobe_cam_pulse(uint32_t microseconds) {
    /* Refuse silently while the calibration sweep is sustaining DIAG HIGH.
     * Consistent with strobe_fire's hold gate; dispatcher decides whether to
     * surface a LOG line. */
    if (s_hold_active) return;

    if (microseconds < 1u)      microseconds = 1u;
    if (microseconds > 100000u) microseconds = 100000u;

    gpio_put(PIN_CAM2_XTR, 0);
    busy_wait_us(microseconds);
    gpio_put(PIN_CAM2_XTR, 1);
}

bool strobe_is_idle(void) {
    return !dma_channel_is_busy(STROBE_DMA_CHAN);
}

float    strobe_get_pulse_width_us(void) { return s_pulse_width_us; }
uint8_t  strobe_get_interval_count(void) { return s_interval_count; }
const float *strobe_get_intervals(void)  { return s_intervals_ms;  }

/* --- Sustained-on hold for Pi-side LED-current calibration ---------------
 *
 * The Pi's strobe_calibration_manager.py used to assert BCM GPIO 10 directly
 * to keep DIAG HIGH while it swept the DAC and read the ADC. With the Pico
 * owning the strobe pin now, the Pi instead sends `CFG STROBE_HOLD=1` over
 * USB CDC, we drive PIN_STROBE_OUT HIGH from SIO (taking the pin away from
 * the PIO state machine), and we re-route back to PIO on STROBE_HOLD=0.
 *
 * A 200 ms hard timeout (STROBE_MAX_HOLD_MS) auto-releases the hold even if
 * the host never sends the deassert — protects the IR LEDs from sustained
 * DC overcurrent if the host crashes mid-sweep.
 */

bool strobe_hold_assert(void) {
    /* Refuse if a fire is in flight: asserting hold would race with the
     * PIO grabbing the pin back. */
    if (dma_channel_is_busy(STROBE_DMA_CHAN)) {
        return false;  /* a fire is in-flight; refuse the hold */
    }

    /* Take pin away from PIO, drive HIGH via SIO. */
    gpio_set_function(PIN_STROBE_OUT, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_STROBE_OUT, GPIO_OUT);
    gpio_put(PIN_STROBE_OUT, 1);

    s_hold_active = true;
    s_hold_deadline_us = time_us_64() + (uint64_t)STROBE_MAX_HOLD_MS * 1000u;
    return true;
}

void strobe_hold_release(void) {
    if (!s_hold_active) return;

    /* Drive LOW first so the gate driver sees the deassert via R8 pulldown
     * BEFORE we hand the pin back to PIO (which will idle it LOW too, but
     * being explicit costs nothing and avoids any glitches during the
     * function-select change). */
    gpio_put(PIN_STROBE_OUT, 0);

    /* Return pin to PIO control so the next strobe_fire() works. The PIO
     * state machine for ir_strobe lives on STROBE_PIO (pio1) so the
     * function-select is GPIO_FUNC_PIO1. */
    gpio_set_function(PIN_STROBE_OUT, GPIO_FUNC_PIO1);

    s_hold_active = false;
    s_hold_deadline_us = 0;
}

bool strobe_is_held(void) {
    return s_hold_active;
}

void strobe_check_hold_timeout(void) {
    if (!s_hold_active) return;
    if (time_us_64() >= s_hold_deadline_us) {
        /* Safety timeout — Pi never sent the deassert. Drop the line. */
        strobe_hold_release();
    }
}
