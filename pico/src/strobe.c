/*
 * strobe.c — IR strobe pulse-train compiler + PIO/DMA driver.
 *
 * Waveform is pre-compiled into a RAM buffer at config time so the fire path is
 * just GPIO write + DMA kick: latency from impact to first PIO edge is
 * dominated by the 1 ms camera setup, not the strobe.
 *
 * Pattern encoding (matches ir_strobe.pio):
 *   word[31..16] = low_count - 1   (cycles LOW after the pulse)
 *   word[15..0]  = high_count - 1  (cycles HIGH for the pulse)
 *
 * One pulse + gap = one 32-bit word IF the gap fits in 16 bits. At 125 MHz,
 * 16-bit max = 65535 cycles = 524.28 µs; longer gaps need follow-up words.
 * high_count must be >= 1 for the PIO loop to terminate, so a long gap encodes
 * as a 1-cycle high blip (8 ns, invisible to the LED driver) then more low.
 *
 * Terminator word high_count=1, low_count=1 ends the pin LOW with the loop in a
 * defined state for the next fire.
 */

#include "strobe.h"

#include <math.h>

#include "pico/stdlib.h"
#include "pico/sync.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#include "ir_strobe.pio.h"
#include "proto.h"
#include "strobe_compile.h"

/* Runtime state lives in core1_usb.c; we read cam_xtr_setup_us on every fire. */
extern volatile pitrac_state_t g_state;

/* STROBE_PATTERN_MAX_WORDS in config.h so compiler and host tests share the bound. */
static uint32_t s_pattern[STROBE_PATTERN_MAX_WORDS];
static uint32_t s_pattern_len = 0;

/* Local copy of tunables so strobe_get_*() can answer STATUS without cross-module reach. */
static float    s_pulse_width_us = STROBE_DEFAULT_PULSE_WIDTH_US;
static float    s_intervals_ms[STROBE_MAX_PULSES];
static uint8_t  s_interval_count = 0;

/* --- Sustained-hold state ----------------------------------------------- */

/* When true, PIN_STROBE_OUT is owned by SIO (driven HIGH) not the PIO sm.
 * Pi-side calibration sweep holds the gate driver on for an ADC reading. */
static volatile bool      s_hold_active   = false;
static volatile uint64_t  s_hold_deadline_us = 0;

/* --- Shared-ADC mutual exclusion ----------------------------------------
 *
 * One ADC mux: core 0 selects CUR_SENSE for the whole FIRE_PEAK sweep (tens of
 * ms), core 1 selects VSYS for a brief STATUS read. A STATUS mid-sweep would
 * reprogram the mux and corrupt both. Can't hold a spinlock across the 60 ms
 * sweep, so core 0 raises s_peak_in_flight (under the lock) for the sweep and
 * core 1 refuses VSYS while it's set. Mux-touching reads take the lock so the
 * flag check and mux select can't interleave. */
static spin_lock_t       *s_adc_spin     = NULL;
static volatile bool      s_peak_in_flight = false;
static uint32_t           s_adc_save_irq = 0;   /* IRQ state across acquire/release */

/* Cycle math lives in strobe_compile.c (pure, host-tested); driven here with the
 * live PIO clock. */

bool strobe_set_pulse_train(const float *intervals_ms,
                            uint8_t count,
                            float pulse_width_us) {
    /* Compile into scratch and only commit on success, so a rejected train
     * (overflow, bad width, energy cap) leaves the working pattern intact rather
     * than stranding a half-built, terminator-less one. */
    /* static (not stack): 2 KB at 512 words is too big for core 1's stack, and
     * this only runs on the core-1 command path with no reentrancy. */
    static uint32_t scratch[STROBE_PATTERN_MAX_WORDS];
    uint32_t scratch_len = 0;

    /* PIO clock = sysclk (clkdiv 1.0f); 125 MHz -> 8 ns/cycle. */
    const float pio_hz = (float)clock_get_hz(clk_sys);
    if (!strobe_compile_pulse_train(intervals_ms, count, pulse_width_us, pio_hz,
                                    scratch, STROBE_PATTERN_MAX_WORDS, &scratch_len)) {
        return false;
    }

    /* Publish the length last so a concurrent core-0 fire never sees a length
     * longer than the words backing it. */
    for (uint32_t i = 0; i < scratch_len; ++i) s_pattern[i] = scratch[i];
    s_pattern_len = scratch_len;

    s_pulse_width_us = pulse_width_us;
    s_interval_count = count;
    for (uint8_t i = 0; i < count; ++i) s_intervals_ms[i] = intervals_ms[i];
    return true;
}

bool strobe_init(void) {
    /* Serialise ADC mux access between core 0's FIRE_PEAK sweep and core 1's
     * VSYS reads (see s_adc_spin notes above). */
    if (s_adc_spin == NULL) {
        int sl = spin_lock_claim_unused(true);
        s_adc_spin = spin_lock_instance((uint)sl);
    }

    /* Cam2 XTR — straight GPIO, active low, idle HIGH. */
    gpio_init(PIN_CAM2_XTR);
    gpio_set_dir(PIN_CAM2_XTR, GPIO_OUT);
    gpio_put(PIN_CAM2_XTR, 1);

    /* Cam1 XTR — same, idle HIGH. Reserved for future stereo; harmless on
     * single-camera installs. */
    gpio_init(PIN_CAM1_XTR);
    gpio_set_dir(PIN_CAM1_XTR, GPIO_OUT);
    gpio_put(PIN_CAM1_XTR, 1);

    /* Strobe pin floats between reset and PIO claiming it, which can briefly
     * trip the MOSFET gate. Drive LOW first so the LED can't light during boot. */
    gpio_init(PIN_STROBE_OUT);
    gpio_set_dir(PIN_STROBE_OUT, GPIO_OUT);
    gpio_put(PIN_STROBE_OUT, 0);

    if (!pio_can_add_program(STROBE_PIO, &ir_strobe_program)) {
        return false;
    }
    uint pio_offset = pio_add_program(STROBE_PIO, &ir_strobe_program);
    ir_strobe_program_init(STROBE_PIO, STROBE_PIO_SM, pio_offset,
                           PIN_STROBE_OUT, 1.0f /* clkdiv: sysclk-rate */);

    /* DMA: 32-bit, read-increment, write fixed to PIO TX FIFO. DREQ = PIO TX so
     * DMA stalls when the FIFO is full. */
    dma_channel_config c = dma_channel_get_default_config(STROBE_DMA_CHAN);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(STROBE_PIO, STROBE_PIO_SM, true));

    /* Configure but don't start — read addr + count get re-set per strobe_fire(). */
    dma_channel_configure(
        STROBE_DMA_CHAN,
        &c,
        &STROBE_PIO->txf[STROBE_PIO_SM],  /* write to PIO TX FIFO */
        s_pattern,                         /* read from pattern buffer (placeholder) */
        0,                                 /* count: 0 = inactive */
        false                              /* don't start yet */
    );

    /* Default compile moved out of strobe_init() to after stdio_init_all() in
     * main.c: pattern compilation's float math (Cortex-M0+ softfloat) was
     * implicated in boot-time TinyUSB corruption causing silent CDC TX. */
    return true;
}

/* Fire-time guard: pattern present, not held, every word's high_count within the
 * per-pulse-width budget. The high-count scan is cheap defense-in-depth — stack
 * corruption poking 0xFFFF into a word's low 16 bits would hold the LED on for
 * 524 ms at 125 MHz, past the boost cap's spec. */
static bool strobe_fire_ok(void) {
    if (s_pattern_len == 0u) return false;

    /* Re-kicking the DMA mid-train corrupts the in-flight transfer. Critical
     * because FIRE_IN (GP9 ISR) and the mic/manual loop path can both fire: the
     * ISR train runs tens of ms and last_event_us isn't stamped until it
     * finalizes, so the inter-shot cooldown won't catch a loop-side overlap. */
    if (dma_channel_is_busy(STROBE_DMA_CHAN)) return false;

    /* Pin is owned by SIO during a calibration hold — PIO can't drive it. */
    if (s_hold_active) return false;

    const uint32_t pio_hz = (uint32_t)clock_get_hz(clk_sys);
    const uint32_t max_high =
        (uint32_t)(STROBE_MAX_PULSE_WIDTH_US * 1e-6f * (float)pio_hz);
    for (uint32_t i = 0; i < s_pattern_len; ++i) {
        if ((s_pattern[i] & 0xFFFFu) > max_high) return false;
    }
    return true;
}

/* Open the camera shutter and kick the DMA train; no completion wait — PIO runs
 * it autonomously. `allow_pre_trigger_delay` gates the host-tunable settle pause;
 * it's a sleep_ms (yields), so the FIRE_IN ISR path passes false. */
static void strobe_kick_dma(bool allow_pre_trigger_delay) {
    /* Pre-trigger settle pause before touching the camera, used by putts. 0 by
     * default. ISR path can't sleep, so only loop callers allow it. */
    if (allow_pre_trigger_delay && g_state.pre_trigger_delay_ms > 0u) {
        /* Chunk and feed the watchdog between chunks: host can CFG this up to
         * 10 s, and one sleep_ms that long would outlast the 2 s watchdog. */
        uint32_t remaining_ms = g_state.pre_trigger_delay_ms;
        while (remaining_ms > 0u) {
            uint32_t chunk = remaining_ms > 100u ? 100u : remaining_ms;
            sleep_ms(chunk);
            watchdog_update();
            remaining_ms -= chunk;
        }
    }

    /* Open shutter (active low) on both triggers in lockstep — single-camera
     * installs ignore PIN_CAM1_XTR, stereo installs get synchronised exposure. */
    gpio_put(PIN_CAM2_XTR, 0);
    gpio_put(PIN_CAM1_XTR, 0);

    /* IMX296/Mira220 shutter response ~few hundred µs. Default 1 ms, host-tunable
     * via `CFG CAM_XTR_SETUP_US=<int>`. busy_wait_us is interrupt-safe. */
    busy_wait_us(g_state.cam_xtr_setup_us);

    /* Re-set read addr + count each time — the channel doesn't auto-rewind.
     * set_trans_count(true) starts immediately. */
    dma_channel_set_read_addr(STROBE_DMA_CHAN, s_pattern, false);
    dma_channel_set_trans_count(STROBE_DMA_CHAN, s_pattern_len, true /* start */);
}

/* Release both XTR pins high. Call once the train has drained. */
static void strobe_release_xtr(void) {
    gpio_put(PIN_CAM2_XTR, 1);
    gpio_put(PIN_CAM1_XTR, 1);
}

bool strobe_fire(void) {
    if (!strobe_fire_ok()) return false;

    strobe_kick_dma(true /* loop context — pre-trigger delay allowed */);

    /* Train is short (max ~30 ms for all 8 pulses at longest gaps), so loop
     * callers just block here. The begin/end pair below is for the FIRE_IN ISR,
     * which must not block. */
    dma_channel_wait_for_finish_blocking(STROBE_DMA_CHAN);

    strobe_release_xtr();
    return true;
}

bool strobe_fire_begin(void) {
    if (!strobe_fire_ok()) return false;

    /* No pre-trigger delay: GP9 FIRE_IN ISR, and the Pi only asserts it when
     * already committed to fire now. */
    strobe_kick_dma(false);
    return true;
}

void strobe_fire_end(void) {
    strobe_release_xtr();
}

bool strobe_fire_peak(uint16_t *peak_adc_out, uint32_t *samples_out) {
    if (peak_adc_out)  *peak_adc_out = 0u;
    if (samples_out)   *samples_out  = 0u;

    if (!strobe_fire_ok()) return false;

    /* Pico W keeps ADC inside main.c's CYW43 guard, so init lazily on the first
     * peak read. */
    static bool s_adc_ready = false;
    if (!s_adc_ready) {
        adc_init();
        adc_gpio_init(PIN_CUR_SENSE_ADC);
        s_adc_ready = true;
    }

    /* Raise the in-flight flag and select CUR_SENSE under the lock so a
     * concurrent core-1 VSYS read sees the flag and backs off; then drop the
     * lock — the flag alone keeps core 1 out for the rest of the sweep. */
    if (s_adc_spin != NULL) {
        uint32_t save = spin_lock_blocking(s_adc_spin);
        s_peak_in_flight = true;
        adc_select_input(ADC_CUR_SENSE_CHANNEL);
        spin_unlock(s_adc_spin, save);
    } else {
        adc_select_input(ADC_CUR_SENSE_CHANNEL);
    }

    strobe_kick_dma(true /* loop context — pre-trigger delay allowed */);

    /* RP2040 ADC ~2 us/sample; with 8.68 us pulses ~4 samples land per pulse-ON
     * window, running max captures the peak. 60 ms deadline keeps a wedged DMA
     * from chewing through the 2 s watchdog. */
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

    /* Release the mux to core 1. */
    if (s_adc_spin != NULL) {
        uint32_t save = spin_lock_blocking(s_adc_spin);
        s_peak_in_flight = false;
        spin_unlock(s_adc_spin, save);
    }

    strobe_release_xtr();

    if (peak_adc_out) *peak_adc_out = peak;
    if (samples_out)  *samples_out  = samples;
    return true;
}

void strobe_cam_pulse(uint32_t microseconds) {
    /* Silent refuse while a calibration hold sustains DIAG HIGH (matches
     * strobe_fire's hold gate). */
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

bool strobe_adc_acquire(void) {
    /* Called before strobe_init claims the lock — nothing to guard. */
    if (s_adc_spin == NULL) return true;

    uint32_t save = spin_lock_blocking(s_adc_spin);
    if (s_peak_in_flight) {
        /* core 0 owns the mux for its sweep. */
        spin_unlock(s_adc_spin, save);
        return false;
    }
    /* Hold the lock until release so the caller's mux select + read can't be
     * interleaved by the peak path flipping the flag. IRQ state stashed in a
     * global — one holder at a time by construction. */
    s_adc_save_irq = save;
    return true;
}

void strobe_adc_release(void) {
    if (s_adc_spin == NULL) return;
    spin_unlock(s_adc_spin, s_adc_save_irq);
}

float    strobe_get_pulse_width_us(void) { return s_pulse_width_us; }
uint8_t  strobe_get_interval_count(void) { return s_interval_count; }
const float *strobe_get_intervals(void)  { return s_intervals_ms;  }

/* --- Sustained-on hold for Pi-side LED-current calibration ---------------
 *
 * Pi sends `CFG STROBE_HOLD=1` over USB CDC; we take PIN_STROBE_OUT from PIO and
 * drive it HIGH via SIO, re-routing back to PIO on STROBE_HOLD=0. (Replaces the
 * old scheme where the Pi asserted BCM GPIO 10 directly to hold DIAG HIGH.)
 *
 * 200 ms hard timeout (STROBE_MAX_HOLD_MS) auto-releases even if the host never
 * deasserts — protects the pulse-rated IR LEDs from DC overcurrent if the host
 * crashes mid-sweep.
 */

bool strobe_hold_assert(void) {
    /* Asserting hold mid-fire would race with PIO grabbing the pin back. */
    if (dma_channel_is_busy(STROBE_DMA_CHAN)) {
        return false;
    }

    /* Take pin from PIO, drive HIGH via SIO. */
    gpio_set_function(PIN_STROBE_OUT, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_STROBE_OUT, GPIO_OUT);
    gpio_put(PIN_STROBE_OUT, 1);

    s_hold_active = true;
    s_hold_deadline_us = time_us_64() + (uint64_t)STROBE_MAX_HOLD_MS * 1000u;
    return true;
}

void strobe_hold_release(void) {
    if (!s_hold_active) return;

    /* Drive LOW (via R8 pulldown) before the function-select handoff so the gate
     * driver sees the deassert without a glitch. */
    gpio_put(PIN_STROBE_OUT, 0);

    /* Return pin to PIO. ir_strobe sm lives on STROBE_PIO (pio1), so
     * GPIO_FUNC_PIO1. */
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
        /* Pi never sent the deassert — drop the line. */
        strobe_hold_release();
    }
}
