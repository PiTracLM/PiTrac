/*
 * main.c — PiTrac Pico firmware entry point.
 *
 * Boot sequence:
 *   1. stdio over USB CDC up (TinyUSB bundled with pico-sdk).
 *   2. Strobe PIO + DMA, default pulse train.
 *   3. I2S RX PIO + DMA, free-running into ring buffer.
 *   4. Impact detector init.
 *   5. Launch core 1 → USB CDC worker.
 *   6. Core 0 enters DSP loop forever:
 *        - read ring buffer
 *        - detect impact
 *        - if armed-and-triggered, fire strobe and notify core 1
 *
 * Why DSP on core 0 and USB on core 1 (not the reverse): TinyUSB is
 * cooperative — it runs whatever scheduler context calls tud_task(). By
 * keeping it on core 1 (where it dominates the core's time), the DSP on
 * core 0 sees deterministic loop timing. The opposite arrangement would
 * mean USB interrupts could perturb the inner sample loop, and we'd lose
 * the worst-case-bounded latency that's critical for impact detection.
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "pico/unique_id.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/watchdog.h"

#include "led.h"

#include "config.h"
#include "core1_usb.h"
#include "impact_detect.h"
#include "init_stage.h"
#include "proto.h"
#include "ring_buffer.h"
#include "strobe.h"

#include "i2s_rx.pio.h"

volatile init_stage_t g_init_stage = INIT_STAGE_NONE;

/* Shared runtime state owned in core1_usb.c — declared volatile because both
 * cores poke at it. We read/write the arm gate from the DSP loop. */
extern volatile pitrac_state_t g_state;

/* --- audio ring + I2S DMA ----------------------------------------------- */

/* The ring lives in BSS — 8 KB at I2S_RING_SAMPLES = 2048. Aligned naturally
 * by virtue of being a struct of uint32_t fields. */
static ring_buffer_t s_audio_ring;

/* DMA control block. The I2S RX channel is configured as a self-chaining
 * pair so it runs forever without CPU intervention:
 *   chan A: reads from PIO RX FIFO, writes to ring storage, chains to B
 *   chan B: writes the ring's read pointer back to chan A's write addr,
 *           re-arms chan A, chains back to A
 *
 * For this firmware we use the simpler "ring DMA" feature of RP2040: a
 * single channel with ring_size set so the write pointer auto-wraps inside
 * the storage area, plus the consumer (DSP) tracks the producer's position
 * by reading the DMA's transfer counter. This avoids needing a second DMA
 * channel and keeps the architecture flat.
 *
 * The "head" of our ring buffer is derived from the DMA transfer count:
 * each time we want to know how many new samples are available, we read
 * the DMA's `transfer_count` register and update s_audio_ring.head from it.
 */

/* Total transfers we've configured the DMA for. We start with a huge count
 * so the DMA effectively never stops — periodically (every ~10 s of audio)
 * we'd re-arm if this firmware ran for hours. For now, 2^31 transfers
 * suffices (≈ 12 hours at 48 kHz × 2 slots). */
#define I2S_DMA_TRANSFERS  (1u << 31)

static volatile uint32_t s_dma_initial_count = I2S_DMA_TRANSFERS;

static void i2s_setup(void) {
    /* Load the I2S RX PIO program. */
    uint offset = pio_add_program(I2S_PIO, &i2s_rx_program);
    i2s_rx_program_init(I2S_PIO, I2S_PIO_SM, offset,
                        PIN_I2S_BCLK,        /* base; LRCLK = base+1 */
                        PIN_I2S_DIN,
                        (float)I2S_SAMPLE_RATE_HZ);

    /* Configure the DMA channel. Reading from PIO RX FIFO, writing into
     * the ring buffer with the write pointer wrapping inside the storage. */
    dma_channel_config c = dma_channel_get_default_config(I2S_DMA_CHAN);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);   /* PIO FIFO is a fixed addr */
    channel_config_set_write_increment(&c, true);
    /* Ring-buffer mode on the write side: the DMA auto-wraps the write
     * pointer inside a 2^I2S_RING_LOG2 × 4-byte region. The +2 is because
     * the ring_size parameter is `log2(bytes)`, not log2(words). */
    channel_config_set_ring(&c, true /* write side */, I2S_RING_LOG2 + 2);
    channel_config_set_dreq(&c, pio_get_dreq(I2S_PIO, I2S_PIO_SM, false /* RX */));

    /* Configure but DO NOT start the DMA yet: we must reset the ring buffer
     * head/tail before any samples can land, otherwise the consumer could
     * race against stale-but-valid head/tail values from a soft-reset. */
    dma_channel_configure(
        I2S_DMA_CHAN,
        &c,
        s_audio_ring.storage,            /* write to ring storage */
        &I2S_PIO->rxf[I2S_PIO_SM],       /* read from PIO RX FIFO */
        I2S_DMA_TRANSFERS,
        false                             /* don't start yet */
    );
    s_dma_initial_count = I2S_DMA_TRANSFERS;

    /* Reset before kicking the DMA so the producer's first write lands in a
     * known-empty ring. */
    ring_buffer_reset(&s_audio_ring);

    dma_channel_start(I2S_DMA_CHAN);
}

/* Tear-safe read of a volatile uint64 written from the other core.
 * Cortex-M0+ has no 64-bit load: the compiler emits two ldr instructions,
 * and the other core can write between them. Classic seqlock-light: read
 * twice and retry until consecutive reads agree. Cheap on uncontended fields
 * (typical case). */
static inline uint64_t read_volatile_u64(const volatile uint64_t *p) {
    uint64_t a, b;
    do {
        a = *p;
        b = *p;
    } while (a != b);
    return a;
}

/* Refresh the ring's head pointer from the DMA's transfer counter. The
 * write address wraps automatically thanks to set_ring; we just need to
 * update head so the consumer can see the new data. */
static inline void i2s_sync_head(void) {
    /* `transfer_count` counts down from initial to 0 — completed transfers
     * = initial - remaining. */
    uint32_t remaining = dma_channel_hw_addr(I2S_DMA_CHAN)->transfer_count;
    uint32_t completed = s_dma_initial_count - remaining;
    s_audio_ring.head = completed;
}

/* --- main --------------------------------------------------------------- */

/* GP9 FIRE_IN rising-edge handler. Calls strobe_fire() directly to meet the
 * <= 2 us latency criterion: dispatching via mailbox would add a multi-ms
 * round trip through the DSP loop. The callback runs on whichever core
 * registered it (core 0 here, the same core that owns strobe state in
 * strobe.c), so the direct call is safe. Kept tiny: hold check, fire,
 * IRQ_OUT pulse, mailbox notify for the EVENT line. */
static void m2_fire_in_irq_callback(uint gpio, uint32_t events) {
    /* Defensive: the SDK calls this for any registered pin/event pair on
     * the same core, so re-check we are actually the FIRE_IN rising edge. */
    if (gpio != PIN_FIRE_IN || (events & GPIO_IRQ_EDGE_RISE) == 0) {
        return;
    }

    /* No cooldown check here. The Pi side gates fire requests; firmware
     * trusts the wire. strobe_is_held protects against firing while the
     * calibration sweep is sustaining DIAG HIGH. */
    if (strobe_is_held()) {
        multicore_fifo_push_blocking(MAILBOX_FIRE_REFUSED_HELD);
        return;
    }

    if (!strobe_fire()) {
        multicore_fifo_push_blocking(MAILBOX_FIRE_REFUSED_HELD);
        return;
    }

    g_state.last_event_us = time_us_64();

    /* IRQ_OUT pulse so the Pi-side ISR can latch the rising edge
     * deterministically (separate from the EVENT line which Pi reads later). */
    gpio_put(PIN_IRQ_OUT, 1);
    busy_wait_us_32(IRQ_OUT_PULSE_US);
    gpio_put(PIN_IRQ_OUT, 0);

    multicore_fifo_push_blocking(MAILBOX_HARDWARE_FIRE_DONE);
}

/* Diagnostic LED-blink helper. Each "checkpoint" blinks the onboard LED N
 * times slowly so we can see how far init got — works without USB, the only
 * channel left when enumeration fails. Each blink is ~200 ms, then a 500 ms
 * pause between groups. The pattern continues forever ONLY if the next
 * checkpoint is never reached; in normal operation each one runs once then
 * the firmware moves on and the heartbeat takes over. */
static void diag_blink(int count) {
    for (int i = 0; i < count; i++) {
        led_set(true);
        sleep_ms(100);
        led_set(false);
        sleep_ms(100);
    }
    sleep_ms(400);
}

int main(void) {
    /* Initialise g_state at runtime instead of via static initialiser. A
     * `volatile` struct with a partial float-array initialiser (intervals_ms
     * is float[32] but the default has 8 entries) was found to corrupt
     * TinyUSB internal state at boot — symptom: USB enumerates but CDC TX
     * delivers zero bytes. Doing this BEFORE any other init avoids any
     * window where another module reads g_state mid-initialisation. */
    g_state_runtime_init();
    init_stage_advance(INIT_STAGE_NONE, INIT_STAGE_RUNTIME_STATE);

    /* PIO + DMA pre-claim, BEFORE cyw43_arch_init. The CYW43 driver does a
     * first-fit `pio_claim_free_sm_and_add_program_for_gpio_range` + two
     * `dma_claim_unused_channel` calls during init. Without pre-claiming our
     * own slots, cyw43 picks PIO1 SM0 (descending search) + DMA 0/1
     * (ascending) — exactly what we want for the strobe + I2S. Our later
     * `pio_add_program(pio1, ...)` then panics from inside the SDK. Pre-
     * claiming forces cyw43's search onto the leftovers (PIO1 SM1 + DMA 2/3).
     * See pico-sdk issue #1351 for the canonical pattern. */
    pio_sm_claim(I2S_PIO,    I2S_PIO_SM);     /* pio0 sm0 */
    pio_sm_claim(STROBE_PIO, STROBE_PIO_SM);  /* pio1 sm0 */
    dma_channel_claim(I2S_DMA_CHAN);          /* 0 */
    dma_channel_claim(STROBE_DMA_CHAN);       /* 1 */
    init_stage_advance(INIT_STAGE_RUNTIME_STATE, INIT_STAGE_PRECLAIM);

    /* LED up FIRST after pre-claim, before anything else can hang. On Pico W
     * this also brings up the CYW43 SPI driver (needed to control the LED).
     * If you never see ANY blink, either the firmware didn't reach main() or
     * cyw43_arch_init failed — most likely cause is a board mismatch
     * (PICO_BOARD vs actual hardware) or a PIO/DMA pre-claim that conflicts. */
    led_init();
    init_stage_advance(INIT_STAGE_PRECLAIM, INIT_STAGE_LED);
    diag_blink(1);   /* checkpoint 1: main() entered, LED path live */

    /* Bring stdio up first so any failure during init can emit a LOG line. */
    stdio_init_all();
    init_stage_advance(INIT_STAGE_LED, INIT_STAGE_STDIO);
    diag_blink(2);   /* checkpoint 2: stdio_init_all returned */

    /* Boot announce. Board ID via flash QSPI is skipped — known to hang on
     * Pico clones with non-Winbond flash. If you need it later, gate it on a
     * post-USB delay so a hang still leaves USB enumerated for debugging. */
    printf("LOG BOOT pitrac-pico fw=%s\n", PITRAC_PICO_FW_VERSION);

    /* g_state was fully initialised at top of main via g_state_runtime_init().
     * Watchdog enable is deferred to just before the DSP loop so init code
     * (PIO program load, DMA configure, core 1 launch) has unlimited time.
     * Once we're in the loop, watchdog_update() runs every iteration. */

    /* (LED was already initialised at top of main for diagnostic blinks.) */
    led_set(false);

#if !PITRAC_LED_VIA_CYW43
    /* Plain-Pico internal-function pins. On Pico W these are owned by the
     * CYW43 driver and we MUST NOT touch them directly — doing so corrupts
     * the WiFi chip's SPI bus, draws unwanted current via WL_ON, and breaks
     * USB enumeration. The CYW43 driver manages SMPS PSM itself. */
    gpio_init(PIN_SMPS_PSM);
    gpio_set_dir(PIN_SMPS_PSM, GPIO_OUT);
    gpio_put(PIN_SMPS_PSM, 1);

    gpio_init(PIN_VBUS_SENSE);
    gpio_set_dir(PIN_VBUS_SENSE, GPIO_IN);

    /* VSYS sense via ADC3 / GPIO 29 — also CYW43-controlled on Pico W. */
    adc_init();
    adc_gpio_init(29);
#endif
    diag_blink(3);       /* checkpoint 3: board-specific init done */
    printf("LOG init: pre-strobe\n");

    /* Strobe first — it's the path that absolutely must work even if
     * everything else fails. Reports back via printf if PIO programs
     * couldn't be loaded. */
    if (!strobe_init()) {
        /* This is rare-to-impossible in practice but worth handling so a
         * silent failure shows up in the host's log. */
        printf("LOG fatal strobe_init failed\n");
        while (1) {
            led_set(true);  sleep_ms(100);
            led_set(false); sleep_ms(100);
        }
    }
    init_stage_advance(INIT_STAGE_STDIO, INIT_STAGE_STROBE_PIO);

    diag_blink(4);   /* checkpoint 4: strobe_init OK */
    printf("LOG init: strobe ok\n");

    /* Compile the default pulse train. Deferred out of strobe_init() because
     * pattern compilation does float math (softfloat helpers on M0+) that was
     * implicated in a boot-time TinyUSB corruption. Safe to do now that stdio
     * is up. */
    {
        const float defaults[] = STROBE_DEFAULT_INTERVALS_MS;
        if (!strobe_set_pulse_train(defaults, STROBE_DEFAULT_INTERVAL_COUNT,
                                    STROBE_DEFAULT_PULSE_WIDTH_US)) {
            printf("LOG warn: strobe_set_pulse_train default failed\n");
        }
    }
    init_stage_advance(INIT_STAGE_STROBE_PIO, INIT_STAGE_STROBE_PATTERN);

    /* I2S — captures audio into the ring buffer. */
    i2s_setup();
    init_stage_advance(INIT_STAGE_STROBE_PATTERN, INIT_STAGE_I2S);
    diag_blink(5);   /* checkpoint 5: i2s_setup returned */
    printf("LOG init: i2s ok\n");

    /* Impact detector — reads from the ring buffer that I2S DMA fills. */
    impact_detect_init(&s_audio_ring);
    init_stage_advance(INIT_STAGE_I2S, INIT_STAGE_IMPACT);
    diag_blink(6);   /* checkpoint 6: impact_detect_init returned */
    printf("LOG init: impact ok\n");

    /* M2 pin claims. HEARTBEAT_OUT goes HIGH here so the Pi can probe it
     * within milliseconds of boot. IRQ_OUT starts LOW; main loop pulses it
     * on every STRIKE / HARDWARE_FIRE. FIRE_IN uses internal pull-down so
     * a disconnected wire reads LOW and never triggers; the boot check
     * below skips the IRQ enable if the line reads HIGH unexpectedly. */
    gpio_init(PIN_HEARTBEAT_OUT);
    gpio_set_dir(PIN_HEARTBEAT_OUT, GPIO_OUT);
    gpio_put(PIN_HEARTBEAT_OUT, 1);

    gpio_init(PIN_IRQ_OUT);
    gpio_set_dir(PIN_IRQ_OUT, GPIO_OUT);
    gpio_put(PIN_IRQ_OUT, 0);

    gpio_init(PIN_FIRE_IN);
    gpio_set_dir(PIN_FIRE_IN, GPIO_IN);
    gpio_pull_down(PIN_FIRE_IN);

    /* Defensive boot check: if the pin reads HIGH at boot with our pull-down
     * active, something is wrong (wire shorted to 3V3, or the Pi is asserting
     * before we are ready). Log + skip the IRQ enable. */
    if (gpio_get(PIN_FIRE_IN)) {
        printf("LOG warn: FIRE_IN reads HIGH at boot, suspicious wiring; IRQ disabled\n");
    } else {
        gpio_set_irq_enabled_with_callback(
            PIN_FIRE_IN,
            GPIO_IRQ_EDGE_RISE,
            true,
            &m2_fire_in_irq_callback);
    }

    init_stage_advance(INIT_STAGE_IMPACT, INIT_STAGE_M2_PINS);

    /* Launch USB CDC worker on core 1. After this point, host commands
     * can arrive at any moment, and core 1 takes over tud_task pumping. */
    printf("LOG init: launching core1\n");
    multicore_launch_core1(core1_usb_entry);
    init_stage_advance(INIT_STAGE_M2_PINS, INIT_STAGE_CORE1);
    diag_blink(7);   /* checkpoint 7: core 1 launched */

    /* Brief settling pause so the I2S filter banks have valid state when
     * the host first looks at us. The IIR filters need ~32 samples (~2 ms)
     * to converge from zero state. */
    sleep_ms(20);
    diag_blink(8);   /* checkpoint 8: ready to enter DSP loop */
    printf("LOG init: entering dsp loop\n");

    /* Now arm the watchdog. Anything that hangs from here on will reset the
     * Pico (strobe pin reverts to gpio_init's LOW default, safe). pause_on_
     * debug=true keeps gdb sessions alive across breakpoints. */
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);
    init_stage_advance(INIT_STAGE_CORE1, INIT_STAGE_WATCHDOG);

    absolute_time_t led_next_toggle = make_timeout_time_ms(500);
    bool led_state = false;

    /* --- main DSP loop ------------------------------------------------ */

    while (true) {
        /* Feed the watchdog every loop iteration. If we're alive enough to
         * get here, the firmware is making progress; a hang anywhere else
         * (mailbox push wedged, hardfault) misses this update and we reset. */
        watchdog_update();

        /* Safety net for STROBE_HOLD: if the Pi-side calibration sweep
         * asserted DIAG HIGH and crashed without releasing, this auto-drops
         * the line after STROBE_MAX_HOLD_MS to protect the IR LEDs. */
        strobe_check_hold_timeout();

        /* Arm-timeout: if the host armed us and walked away, drop the gate
         * so an unattended Pico doesn't strobe on random room noise. We
         * push the LOG to core 1 via the mailbox rather than printf-ing
         * from core 0 — stdio over USB CDC is core-1-owned and concurrent
         * writes from core 0 would risk garbled output. */
        if (g_state.armed && time_us_64() > read_volatile_u64(&g_state.arm_deadline_us)) {
            g_state.armed = false;
            multicore_fifo_push_blocking(MAILBOX_DISARM_TIMEOUT);
        }

        /* Update the ring head from the DMA counter so the detector sees
         * fresh data. */
        i2s_sync_head();

        /* Run impact detection. Reads what's available, returns true with
         * RMS value if it just fired a trigger. */
        int32_t rms = 0;
        bool triggered = impact_detect_step(g_state.armed, &rms);

        if (triggered) {
            g_state.last_rms = rms;

            /* Cooldown gate. last_event_us is updated on EVERY fire (both
             * mic-triggered and manual) — core 0 is the SOLE writer to avoid
             * the dual-writer race that core 1's prior bookkeeping introduced.
             * If the boost cap hasn't recharged yet, refuse and log — better
             * a missed shot than a brown-out during a dim train. */
            uint64_t now_us = time_us_64();
            uint64_t last_us = read_volatile_u64(&g_state.last_event_us);
            uint64_t min_gap_us = (uint64_t)g_state.min_inter_shot_ms * 1000u;
            if (last_us != 0 && now_us - last_us < min_gap_us) {
                multicore_fifo_push_blocking(MAILBOX_FIRE_REFUSED_COOLDOWN);
                /* Still consume the arm — a host that triggered noise during
                 * cooldown has to explicitly re-arm anyway. */
                g_state.armed = false;
                multicore_fifo_push_blocking(MAILBOX_DISARM_AFTER_FIRE);
            } else {
                /* Fire the strobe + camera trigger. This blocks until the DMA
                 * train completes (~tens of ms worst case); during that window
                 * we won't process new audio, but the ring buffer is large
                 * enough to soak it, and the debounce window is 300 ms anyway
                 * so we won't re-trigger immediately. */
                if (strobe_fire()) {
                    g_state.last_event_us = time_us_64();
                    /* EVENT before disarm-LOG so the wire order reads
                     * naturally (EVENT first, consequence second). */
                    multicore_fifo_push_blocking(MAILBOX_STRIKE);

                    /* Pi-side IRQ on rising edge. Stay HIGH for 100 us so a
                     * polled sampler can't miss the edge. */
                    gpio_put(PIN_IRQ_OUT, 1);
                    busy_wait_us_32(IRQ_OUT_PULSE_US);
                    gpio_put(PIN_IRQ_OUT, 0);
                } else {
                    multicore_fifo_push_blocking(MAILBOX_FIRE_REFUSED_HELD);
                }

                /* Disarm even on refusal: noise during a held sweep still
                 * burned the one-shot arm. */
                g_state.armed = false;
                multicore_fifo_push_blocking(MAILBOX_DISARM_AFTER_FIRE);
            }
        }

        /* Check for a manual FIRE request from core 1. Non-blocking — if
         * no message is waiting, just continue the DSP loop. */
        if (multicore_fifo_rvalid()) {
            uint32_t msg = multicore_fifo_pop_blocking();
            if (msg == MAILBOX_MANUAL_FIRE) {
                /* Same cooldown gate as the mic-triggered path. Calibration
                 * sweeps can lower min_inter_shot_ms via CFG when each fire
                 * is a tiny single pulse. */
                uint64_t now_us = time_us_64();
                uint64_t last_us = read_volatile_u64(&g_state.last_event_us);
                uint64_t min_gap_us = (uint64_t)g_state.min_inter_shot_ms * 1000u;
                if (last_us != 0 && now_us - last_us < min_gap_us) {
                    multicore_fifo_push_blocking(MAILBOX_FIRE_REFUSED_COOLDOWN);
                } else {
                    if (strobe_fire()) {
                        g_state.last_event_us = time_us_64();
                        multicore_fifo_push_blocking(MAILBOX_MANUAL_FIRE_DONE);
                    } else {
                        multicore_fifo_push_blocking(MAILBOX_FIRE_REFUSED_HELD);
                    }
                    /* Same disarm-after-fire policy for manual fires — keeps
                     * the arm gate predictable: ARMED=1 is a one-shot intent.
                     * Only notify when we actually changed state to avoid
                     * misleading lines on FIRE-while-already-disarmed. */
                    if (g_state.armed) {
                        g_state.armed = false;
                        multicore_fifo_push_blocking(MAILBOX_DISARM_AFTER_FIRE);
                    }
                }
            }
        }

        /* Status LED heartbeat. Slow blink (1 Hz) when disarmed, fast
         * (5 Hz) when armed. Cheap visual indicator while bench testing. */
        if (absolute_time_diff_us(get_absolute_time(), led_next_toggle) <= 0) {
            led_state = !led_state;
            led_set(led_state);
            uint32_t period = g_state.armed ? 100 : 500;
            led_next_toggle = make_timeout_time_ms(period);
        }
    }

    /* Unreachable. */
    return 0;
}
