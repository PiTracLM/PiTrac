/*
 * main.c — PiTrac Pico firmware entry point.
 *
 * Boot: stdio/USB-CDC → strobe PIO+DMA → I2S RX PIO+DMA → impact detector →
 * core 1 (USB CDC) → core 0 DSP loop (read ring, detect, fire+notify).
 *
 * DSP on core 0, USB on core 1: TinyUSB is cooperative (runs in whatever
 * context calls tud_task), so pinning it to core 1 gives the DSP loop
 * deterministic, worst-case-bounded timing — critical for impact detection.
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
#include "sensor.h"
#include "sensor_mic.h"
#include "init_stage.h"
#include "proto.h"
#include "ring_buffer.h"
#include "strobe.h"
#include "arm_gate.h"

#include "i2s_rx.pio.h"

volatile init_stage_t g_init_stage = INIT_STAGE_NONE;

/* Shared runtime state owned in core1_usb.c; volatile, touched by both cores. */
extern volatile pitrac_state_t g_state;

/* Arm gate + keep-alive deadline, core 0 only. g_state.armed mirrors
 * s_arm_gate.armed for the DSP/strobe hot path; deadline owned by the gate. */
static arm_gate_t s_arm_gate;

/* --- audio ring + I2S DMA ----------------------------------------------- */

/* 8 KB in BSS at I2S_RING_SAMPLES = 2048; naturally aligned (struct of u32). */
static ring_buffer_t s_audio_ring;

/* I2S RX uses RP2040 ring-DMA: a single channel with ring_size set so the
 * write pointer auto-wraps inside storage. The consumer (DSP) tracks the
 * producer by reading the DMA transfer_count register into s_audio_ring.head. */

/* DMA transfer count; 2^31 ≈ 6.2 h at 96 kHz (48 kHz x 2 slots). */
#define I2S_DMA_TRANSFERS  (1u << 31)

static volatile uint32_t s_dma_initial_count = I2S_DMA_TRANSFERS;

/* Re-arm before the finite count hits zero or the producer freezes and the
 * detector goes deaf. This margin (~21 ms at 96 kHz) fits inside one DSP
 * loop, so no audible gap. */
#define I2S_DMA_REARM_MARGIN  ((uint32_t)I2S_RING_SAMPLES)

/* Configure + (re)start I2S RX DMA into a freshly reset ring. Shared by boot
 * and the loop's re-arm so both use the proven path. Safe on a running channel
 * (aborts first). Resetting head/tail to 0 realigns L/R slot parity (ring-index
 * even = L), the invariant i2s_sync_head's even-skip guard protects. */
static void i2s_dma_arm(void) {
    dma_channel_abort(I2S_DMA_CHAN);

    dma_channel_config c = dma_channel_get_default_config(I2S_DMA_CHAN);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);   /* PIO FIFO is a fixed addr */
    channel_config_set_write_increment(&c, true);
    /* Write-side ring wraps inside a 2^I2S_RING_LOG2 x 4-byte region. +2 since
     * ring_size is log2(bytes), not log2(words). */
    channel_config_set_ring(&c, true /* write side */, I2S_RING_LOG2 + 2);
    channel_config_set_dreq(&c, pio_get_dreq(I2S_PIO, I2S_PIO_SM, false /* RX */));

    dma_channel_configure(
        I2S_DMA_CHAN,
        &c,
        s_audio_ring.storage,            /* write to ring storage */
        &I2S_PIO->rxf[I2S_PIO_SM],       /* read from PIO RX FIFO */
        I2S_DMA_TRANSFERS,
        false                             /* don't start yet */
    );
    s_dma_initial_count = I2S_DMA_TRANSFERS;

    /* Reset before kicking the DMA so the first write lands in an empty ring. */
    ring_buffer_reset(&s_audio_ring);

    dma_channel_start(I2S_DMA_CHAN);
}

static void i2s_setup(void) {
    uint offset = pio_add_program(I2S_PIO, &i2s_rx_program);
    i2s_rx_program_init(I2S_PIO, I2S_PIO_SM, offset,
                        PIN_I2S_BCLK,        /* base; LRCLK = base+1 */
                        PIN_I2S_DIN,
                        (float)I2S_SAMPLE_RATE_HZ);
    i2s_dma_arm();
}

/* Re-arm the capture DMA if it's about to run out of transfers. Returns true
 * (and bumps the rearm counter) on a re-arm so the caller clears the stale
 * overrun latch (ring is freshly reset). Called every loop; re-arms ~every 6 h. */
static bool i2s_rearm_if_low(void) {
    if (dma_channel_hw_addr(I2S_DMA_CHAN)->transfer_count >= I2S_DMA_REARM_MARGIN) {
        return false;
    }
    i2s_dma_arm();
    g_state.dma_rearm_count++;
    return true;
}

/* Tear-safe read of a volatile u64 written from the other core. Cortex-M0+ has
 * no 64-bit load (two ldr's, other core can write between); read twice, retry
 * until consecutive reads agree. */
static inline uint64_t read_volatile_u64(const volatile uint64_t *p) {
    uint64_t a, b;
    do {
        a = *p;
        b = *p;
    } while (a != b);
    return a;
}

/* Boost-cap cooldown gate, shared by the mic-trigger and both manual-fire paths:
 * true once at least min_inter_shot_ms has elapsed since the last fire, so the
 * next fire won't brown out the rail. last_event_us == 0 means "never fired". */
static inline bool cooldown_elapsed(void) {
    uint64_t last_us = read_volatile_u64(&g_state.last_event_us);
    if (last_us == 0u) return true;
    return (time_us_64() - last_us) >= (uint64_t)g_state.min_inter_shot_ms * 1000u;
}

/* Refresh ring head from the DMA transfer counter, guard against producer
 * overrun. Returns true if it dropped stale audio (caller LOGs once).
 *
 * A fire blocks the DSP loop tens of ms in strobe_fire() while the DMA keeps
 * filling at 48 kHz. If it laps the consumer, slots between tail and
 * (head - capacity) are already overwritten (torn audio); skip tail forward to
 * the newest full window. We're the sole writer of tail, so this races nothing.
 * Skip count is forced even to keep the detector's per-word L/R parity aligned. */
static inline bool i2s_sync_head(void) {
    /* transfer_count counts down: completed = initial - remaining. */
    uint32_t remaining = dma_channel_hw_addr(I2S_DMA_CHAN)->transfer_count;
    uint32_t completed = s_dma_initial_count - remaining;
    s_audio_ring.head = completed;

    uint32_t available = s_audio_ring.head - s_audio_ring.tail;
    if (available <= I2S_RING_SAMPLES) {
        return false;
    }

    /* Keep only the newest full ring; even skip preserves L/R phase. */
    uint32_t new_tail = s_audio_ring.head - I2S_RING_SAMPLES;
    if (((new_tail - s_audio_ring.tail) & 1u) != 0u) {
        new_tail += 1u;   /* drop one more word — stays an even skip, in-bounds */
    }
    s_audio_ring.tail = new_tail;
    return true;
}

/* --- main --------------------------------------------------------------- */

/* Post a telemetry/notify line to core 1. core0 → core1 messages are all
 * droppable (LOG/EVENT): on a full FIFO, dropping beats blocking the DSP loop
 * and missing the watchdog feed. Control state never travels this way. */
static inline void notify_core1(uint32_t msg) {
    multicore_fifo_push_timeout_us(msg, FIFO_PUSH_TIMEOUT_US);
}

/* Set by the FIRE_IN ISR once it kicked a hardware fire; the DSP loop finalises
 * when the train drains and emits the event. Core 0 only (ISR sets, loop
 * clears); ISR refuses a second fire in flight, so at most one finalise pends. */
static volatile bool s_hw_fire_pending = false;

/* GP9 FIRE_IN rising-edge handler. The first edge (camera shutter + strobe
 * train start) happens here to keep FIRE_IN → shutter latency tight. The tail
 * is deferred: strobe_fire() would block the whole train (tens of ms) in
 * dma_channel_wait_for_finish_blocking and could sleep_ms on the pre-trigger
 * delay — illegal in interrupt context. strobe_fire_begin() kicks the DMA and
 * returns; the DSP loop releases XTR and emits EVENT once strobe_is_idle().
 * Callback runs on core 0, which owns strobe state, so no cross-core hop. */
static void m2_fire_in_irq_callback(uint gpio, uint32_t events) {
    /* SDK calls this for any registered pin/event on the core; re-check ours. */
    if (gpio != PIN_FIRE_IN || (events & GPIO_IRQ_EDGE_RISE) == 0) {
        return;
    }

    /* No cooldown here — the Pi gates fire requests; firmware trusts the wire.
     * strobe_is_held guards against firing while the calibration sweep sustains
     * DIAG HIGH. Never block a push from ISR context: drop the telemetry-only
     * refusal on a full FIFO rather than wedge. */
    if (strobe_is_held() || !strobe_fire_begin()) {
        multicore_fifo_push_timeout_us(MAILBOX_FIRE_REFUSED_HELD, FIFO_PUSH_TIMEOUT_US);
        return;
    }

    /* IRQ_OUT pulse for the Pi-side ISR to latch the edge (separate from the
     * EVENT line Pi reads later). */
    gpio_put(PIN_IRQ_OUT, 1);
    busy_wait_us_32(IRQ_OUT_PULSE_US);
    gpio_put(PIN_IRQ_OUT, 0);

    s_hw_fire_pending = true;
}

/* Diagnostic blink: N slow onboard-LED pulses marking how far init got, the
 * only channel left when USB enumeration fails. */
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
    /* Runtime-init g_state, not a static initialiser: a volatile struct with a
     * partial float-array init (intervals_ms is float[32], default has 8) was
     * found to corrupt TinyUSB at boot (USB enumerates, CDC TX delivers zero
     * bytes). First, before any module can read g_state mid-init. */
    g_state_runtime_init();
    init_stage_advance(INIT_STAGE_NONE, INIT_STAGE_RUNTIME_STATE);

    /* PIO + DMA pre-claim BEFORE cyw43_arch_init: the CYW43 driver first-fits
     * a PIO SM + two DMA channels during init and would grab the slots we want
     * for strobe + I2S, panicking our later pio_add_program(pio1). Pre-claiming
     * forces its search onto the leftovers (PIO1 SM1 + DMA 2/3). See pico-sdk
     * issue #1351. */
    pio_sm_claim(I2S_PIO,    I2S_PIO_SM);     /* pio0 sm0 */
    pio_sm_claim(STROBE_PIO, STROBE_PIO_SM);  /* pio1 sm0 */
    dma_channel_claim(I2S_DMA_CHAN);          /* 0 */
    dma_channel_claim(STROBE_DMA_CHAN);       /* 1 */
    init_stage_advance(INIT_STAGE_RUNTIME_STATE, INIT_STAGE_PRECLAIM);

    /* LED up first after pre-claim, before anything can hang. On Pico W this
     * also brings up the CYW43 SPI driver. No blink at all means main() wasn't
     * reached or cyw43_arch_init failed — usually a board mismatch (PICO_BOARD)
     * or a conflicting PIO/DMA pre-claim. */
    led_init();
    init_stage_advance(INIT_STAGE_PRECLAIM, INIT_STAGE_LED);
    diag_blink(1);   /* checkpoint 1: main() entered, LED path live */

    /* stdio up early so init failures can emit a LOG line. */
    stdio_init_all();
    init_stage_advance(INIT_STAGE_LED, INIT_STAGE_STDIO);
    diag_blink(2);   /* checkpoint 2: stdio_init_all returned */

    /* Board ID via flash QSPI skipped — hangs on clones with non-Winbond
     * flash. If re-added, gate behind a post-USB delay so a hang still leaves
     * USB enumerated. */
    printf("LOG BOOT pitrac-pico fw=%s\n", PITRAC_PICO_FW_VERSION);

    /* Watchdog deferred to just before the DSP loop so init (PIO load, DMA
     * configure, core 1 launch) has unlimited time. */

    led_set(false);

#if !PITRAC_LED_VIA_CYW43
    /* Plain-Pico internal-function pins. On Pico W these belong to the CYW43
     * driver — touching them corrupts its SPI bus, draws current via WL_ON,
     * and breaks USB enumeration. CYW43 manages SMPS PSM itself. */
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

    /* Strobe first — the path that must work even if everything else fails. */
    if (!strobe_init()) {
        printf("LOG fatal strobe_init failed\n");
        while (1) {
            led_set(true);  sleep_ms(100);
            led_set(false); sleep_ms(100);
        }
    }
    init_stage_advance(INIT_STAGE_STDIO, INIT_STAGE_STROBE_PIO);

    diag_blink(4);   /* checkpoint 4: strobe_init OK */
    printf("LOG init: strobe ok\n");

    /* Compile the default pulse train. Deferred out of strobe_init(): the
     * float math (softfloat on M0+) was implicated in boot-time TinyUSB
     * corruption; safe now that stdio is up. */
    {
        const float defaults[] = STROBE_DEFAULT_INTERVALS_MS;
        if (!strobe_set_pulse_train(defaults, STROBE_DEFAULT_INTERVAL_COUNT,
                                    STROBE_DEFAULT_PULSE_WIDTH_US)) {
            printf("LOG warn: strobe_set_pulse_train default failed\n");
        }
    }
    init_stage_advance(INIT_STAGE_STROBE_PIO, INIT_STAGE_STROBE_PATTERN);

    i2s_setup();
    init_stage_advance(INIT_STAGE_STROBE_PATTERN, INIT_STAGE_I2S);
    diag_blink(5);   /* checkpoint 5: i2s_setup returned */
    printf("LOG init: i2s ok\n");

    sensor_mic_set_ring(&s_audio_ring);
    sensors_init_all();
    init_stage_advance(INIT_STAGE_I2S, INIT_STAGE_IMPACT);
    diag_blink(6);   /* checkpoint 6: impact_detect_init returned */
    printf("LOG init: impact ok\n");

    /* M2 pin claims. HEARTBEAT_OUT HIGH so the Pi can probe it right after
     * boot. IRQ_OUT starts LOW, pulsed on every STRIKE/HARDWARE_FIRE. FIRE_IN
     * pulled down so a disconnected wire reads LOW and never triggers. */
    gpio_init(PIN_HEARTBEAT_OUT);
    gpio_set_dir(PIN_HEARTBEAT_OUT, GPIO_OUT);
    gpio_put(PIN_HEARTBEAT_OUT, 1);

    gpio_init(PIN_IRQ_OUT);
    gpio_set_dir(PIN_IRQ_OUT, GPIO_OUT);
    gpio_put(PIN_IRQ_OUT, 0);

    gpio_init(PIN_FIRE_IN);
    gpio_set_dir(PIN_FIRE_IN, GPIO_IN);
    gpio_pull_down(PIN_FIRE_IN);

    /* HIGH at boot with our pull-down active = shorted to 3V3 or Pi asserting
     * early. Log + skip the IRQ enable. */
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

    /* Launch USB CDC worker on core 1; from here host commands can arrive any
     * time and core 1 owns tud_task pumping. */
    printf("LOG init: launching core1\n");
    multicore_launch_core1(core1_usb_entry);
    init_stage_advance(INIT_STAGE_M2_PINS, INIT_STAGE_CORE1);
    diag_blink(7);   /* checkpoint 7: core 1 launched */

    /* Settling pause: the IIR filter banks need ~32 samples (~2 ms) to
     * converge from zero state before the host first looks at us. */
    sleep_ms(20);
    diag_blink(8);   /* checkpoint 8: ready to enter DSP loop */
    printf("LOG init: entering dsp loop\n");

    /* Arm watchdog: a hang from here resets the Pico (strobe pin reverts to
     * gpio_init's LOW default, safe). pause_on_debug=true keeps gdb alive
     * across breakpoints. */
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);
    init_stage_advance(INIT_STAGE_CORE1, INIT_STAGE_WATCHDOG);

    absolute_time_t led_next_toggle = make_timeout_time_ms(500);
    bool led_state = false;
    bool ring_overflow_logged = false;

    /* --- main DSP loop ------------------------------------------------ */

    while (true) {
        /* Feed the watchdog every loop. A hang elsewhere (wedged mailbox push,
         * hardfault) misses this and resets. */
        watchdog_update();

        /* STROBE_HOLD safety net: if a calibration sweep asserted DIAG HIGH and
         * crashed, auto-drop after STROBE_MAX_HOLD_MS to protect the IR LEDs. */
        strobe_check_hold_timeout();

        /* Arm-timeout: drop the gate if the host armed and walked away, so an
         * unattended Pico doesn't strobe on room noise. LOG via mailbox, not
         * printf — stdio is core-1-owned and a core-0 write would garble. */
        if (arm_gate_poll(&s_arm_gate, time_us_64())) {
            g_state.armed = false;   /* mirror for the DSP/strobe readers */
            notify_core1(MAILBOX_DISARM_TIMEOUT);
        }

        /* Re-arm the capture DMA before its count hits zero; re-arm resets the
         * ring, so clear the overrun latch with it. */
        if (i2s_rearm_if_low()) {
            ring_overflow_logged = false;
        }

        /* Refresh ring head from the DMA counter. If the producer lapped us
         * (typically right after a fire's blocking window), drop stale audio and
         * LOG once per episode — edge-triggered so a sustained overrun can't spam. */
        bool ring_lapped = i2s_sync_head();
        if (ring_lapped && !ring_overflow_logged) {
            notify_core1(MAILBOX_RING_OVERFLOW);
            ring_overflow_logged = true;
        } else if (!ring_lapped) {
            ring_overflow_logged = false;
        }

        int32_t rms = 0;
        bool triggered = sensors_step(g_state.armed, &rms);

        if (triggered) {
            g_state.last_rms = rms;

            /* Cooldown gate. last_event_us updated on EVERY fire (mic + manual);
             * core 0 is the SOLE writer to avoid the dual-writer race core 1's
             * prior bookkeeping introduced. Refuse if the boost cap hasn't
             * recharged — a missed shot beats a brown-out during a dim train. */
            if (!cooldown_elapsed()) {
                notify_core1(MAILBOX_FIRE_REFUSED_COOLDOWN);
                /* Still consume the arm — noise during cooldown forces a re-arm. */
                arm_gate_disarm(&s_arm_gate);
                g_state.armed = false;
                notify_core1(MAILBOX_DISARM_AFTER_FIRE);
            } else {
                /* Fire strobe + camera trigger. Blocks until the DMA train
                 * completes (~tens of ms worst case); the ring soaks the audio
                 * we miss, and the 300 ms debounce prevents an immediate re-trigger. */
                if (strobe_fire()) {
                    g_state.last_event_us = time_us_64();
                    /* EVENT before disarm-LOG so the wire order reads naturally. */
                    notify_core1(MAILBOX_STRIKE);

                    /* Pi-side IRQ: hold HIGH 100 us so a polled sampler can't
                     * miss the edge. */
                    gpio_put(PIN_IRQ_OUT, 1);
                    busy_wait_us_32(IRQ_OUT_PULSE_US);
                    gpio_put(PIN_IRQ_OUT, 0);
                } else {
                    notify_core1(MAILBOX_FIRE_REFUSED_HELD);
                }

                /* Disarm even on refusal: noise during a held sweep burned the
                 * one-shot arm. */
                arm_gate_disarm(&s_arm_gate);
                g_state.armed = false;
                notify_core1(MAILBOX_DISARM_AFTER_FIRE);
            }
        }

        /* Finalise a hardware (GP9) fire once its train drains: release cam XTR,
         * stamp last_event_us (core 0 stays sole writer), emit EVENT.
         * strobe_is_idle() keeps us off the pin until the last pulse is clocked. */
        if (s_hw_fire_pending && strobe_is_idle()) {
            strobe_fire_end();
            g_state.last_event_us = time_us_64();
            s_hw_fire_pending = false;
            notify_core1(MAILBOX_HARDWARE_FIRE_DONE);
        }

        /* Drain one inbound request from core 1 (non-blocking). core 1 posts
         * arm/disarm/manual-fire here; core 0 is the single applier of arm
         * state, so no multi-writer race on g_state.armed or arm_deadline_us.
         * One pop per loop suffices — the host can't outpace the loop. */
        if (multicore_fifo_rvalid()) {
            uint32_t msg = multicore_fifo_pop_blocking();
            if (msg == MAILBOX_REQ_ARM) {
                /* Sole-writer arm: gate computes the deadline from the atomic
                 * 32-bit arm_timeout_ms core 1 set; mirror armed out. */
                arm_gate_arm(&s_arm_gate, time_us_64(), g_state.arm_timeout_ms);
                g_state.armed = true;
            } else if (msg == MAILBOX_REQ_DISARM) {
                arm_gate_disarm(&s_arm_gate);
                g_state.armed = false;
            } else if (msg == MAILBOX_REQ_HEARTBEAT) {
                /* Keep-alive: push the deadline out across the player's swing.
                 * Gate only acts while armed and skips the arm-quiet gate, so
                 * armed is unchanged. */
                arm_gate_heartbeat(&s_arm_gate, time_us_64(), g_state.arm_timeout_ms);
            } else if (msg == MAILBOX_MANUAL_FIRE) {
                /* Same cooldown gate as the mic path. Calibration sweeps can
                 * lower min_inter_shot_ms via CFG (each fire is a single pulse). */
                if (!cooldown_elapsed()) {
                    notify_core1(MAILBOX_FIRE_REFUSED_COOLDOWN);
                } else {
                    if (strobe_fire()) {
                        g_state.last_event_us = time_us_64();
                        notify_core1(MAILBOX_MANUAL_FIRE_DONE);
                    } else {
                        notify_core1(MAILBOX_FIRE_REFUSED_HELD);
                    }
                    /* Disarm-after-fire (ARMED=1 is one-shot intent). Notify
                     * only on a real state change to avoid a misleading line on
                     * FIRE-while-already-disarmed. */
                    if (s_arm_gate.armed) {
                        arm_gate_disarm(&s_arm_gate);
                        g_state.armed = false;
                        notify_core1(MAILBOX_DISARM_AFTER_FIRE);
                    }
                }
            } else if (msg == MAILBOX_MANUAL_FIRE_PEAK) {
                /* Cooldown gate, then fire + sample ADC0 across the train. Result
                 * stashed in last_peak_adc/last_peak_samples before the completion
                 * push (which barriers the other core's read). */
                if (!cooldown_elapsed()) {
                    notify_core1(MAILBOX_FIRE_REFUSED_COOLDOWN);
                } else {
                    uint16_t peak = 0u;
                    uint32_t samples = 0u;
                    if (strobe_fire_peak(&peak, &samples)) {
                        g_state.last_event_us = time_us_64();
                        g_state.last_peak_adc = peak;
                        g_state.last_peak_samples = samples;
                        notify_core1(MAILBOX_MANUAL_FIRE_PEAK_DONE);
                    } else {
                        notify_core1(MAILBOX_FIRE_REFUSED_HELD);
                    }
                    if (s_arm_gate.armed) {
                        arm_gate_disarm(&s_arm_gate);
                        g_state.armed = false;
                        notify_core1(MAILBOX_DISARM_AFTER_FIRE);
                    }
                }
            }
        }

        /* Status LED heartbeat: 1 Hz disarmed, 5 Hz armed. */
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
