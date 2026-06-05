/*
 * config.h — pin assignments, sample rates, and tunable defaults for the
 * PiTrac Pico firmware. Header-only, all compile-time constants.
 *
 * Hardware: original Raspberry Pi Pico (RP2040), driving:
 *   - SPH0645 I2S MEMS mic (Adafruit 3421)
 *   - MCP1407 gate driver feeding a MOSFET → IR LED bank
 *   - Cam2 external trigger (IMX296 / Mira220), active low
 *
 * Pin numbers match README.md wiring; I2S pins are contiguous because RP2040
 * PIO can only address contiguous pin ranges for its sideset + OUT/IN groups.
 */

#ifndef PITRAC_PICO_CONFIG_H
#define PITRAC_PICO_CONFIG_H

#include <stdint.h>

/* ---------------------------------------------------------------- pins ---- */

/* SPH0645 I2S MEMS mic; Pico is master (drives BCLK + LRCLK).
 *
 * BCLK/LRCLK must be adjacent GPIOs: one SM sidesets both over a contiguous
 * span while sampling DOUT via IN. GP10/11/12 keeps them on one side and
 * matches the bottom row of stock SPH0645 breakouts. */
#define PIN_I2S_BCLK   10  /* SCK on most SPH0645 breakouts */
#define PIN_I2S_LRCLK  11  /* WS / LRC / "select" */
#define PIN_I2S_DIN    12  /* SD / DOUT from mic */

/* Strobe output → drives MCP1407 input on the V3 connector board.
 * Driven by ir_strobe.pio at cycle-accurate timing. */
#define PIN_STROBE_OUT 13

/* Cam2 external trigger. IMX296/Mira220 active low: LOW starts exposure, HIGH
 * ends it. Toggled from C — shutter latency dominates, so no cycle-accurate
 * timing vs the strobe needed. */
#define PIN_CAM2_XTR   15

/* M2 pins. Pi side: GPIO 26 -> GP9 (FIRE_IN), GP7 -> Pi GPIO 27 (IRQ_OUT),
 * GP8 -> Pi GPIO 22 (HEARTBEAT_OUT). FIRE_IN uses internal pull-down so a
 * disconnected wire reads LOW and never triggers. */
#define PIN_FIRE_IN        9
#define PIN_IRQ_OUT        7
#define PIN_HEARTBEAT_OUT  8

/* IRQ_OUT pulse width on STRIKE / HARDWARE_FIRE. Pi samples on rising edge;
 * 100 us is above any reasonable poll interval and well under the 200 ms cooldown. */
#define IRQ_OUT_PULSE_US   100u

/* Cam1 external trigger — reserved for future stereo. Same polarity as cam2,
 * driven HIGH at boot; strobe_fire() pulses both cam pins together so
 * single-camera installs are unaffected. */
#define PIN_CAM1_XTR   14

/* Onboard LED — heartbeat / armed-state indicator. GP25 on a stock Pico. */
#define PIN_STATUS_LED 25

/* RP2040 internal-function pins (per Pico datasheet): on-module, not
 * user-routable, but accessed via the regular GPIO API. */

/* VBUS sense — HIGH if 5 V present on micro-USB. Tells whether the Pi pulled
 * USB power or the cable was yanked. Digital read, no ADC. */
#define PIN_VBUS_SENSE   24

/* SMPS power-save control. LOW = PFM (efficient but ripple in audible range);
 * HIGH = forced PWM (quieter, cleaner VSYS/I2S reads). Held HIGH always — the
 * few-mA penalty is worth the noise reduction on a USB-powered build. */
#define PIN_SMPS_PSM     23

/* VSYS/3 on ADC3 (GPIO 29); ×3 the mV reading for real supply voltage.
 * Brownout detection. */
#define ADC_VSYS_CHANNEL 3

/* V3 board CUR-SENSE (TP4) → Pico GPIO 26 = ADC0. No divider — shunt is 0 V
 * idle, ~1 V at 10 A peak, within ADC range. LED current sampling in
 * strobe_fire_peak(). */
#define PIN_CUR_SENSE_ADC      26
#define ADC_CUR_SENSE_CHANNEL  0

/* ----------------------------------------------------------- audio path --- */

/* Source sample rate. SPH0645 tolerates 32–64 fs on BCLK; 48 kHz × 64 =
 * 3.072 MHz BCLK gives 64 BCLK/frame = one 32-bit slot per channel. The mic
 * outputs on one slot only (SEL pin; SEL=GND → left). */
#define I2S_SAMPLE_RATE_HZ      48000u
#define I2S_BITS_PER_SAMPLE     32u    /* slot width — actual data is 18 bits MSB */
#define I2S_BCLK_FREQ_HZ        (I2S_SAMPLE_RATE_HZ * I2S_BITS_PER_SAMPLE * 2)  /* L+R slots */

/* Decimation 48 kHz → 16 kHz: keeps the 2-6 kHz band below Nyquist and cuts
 * ring RAM 3×. A 3-tap average is enough anti-aliasing for detection. */
#define DSP_DECIMATION          3u
#define DSP_SAMPLE_RATE_HZ      (I2S_SAMPLE_RATE_HZ / DSP_DECIMATION)  /* 16000 Hz */

/* Raw I2S ring, in 32-bit slots. ~32 ms at 48 kHz gives the USB worker slack
 * if the Pi steals scheduler time. Power of two — SPSC ring uses bitmasking. */
#define I2S_RING_LOG2           11
#define I2S_RING_SAMPLES        (1u << I2S_RING_LOG2)  /* 2048 samples ≈ 42 ms @ 48 kHz */

/* RMS window: 1 ms at 16 kHz = 16 samples. Short = fast onset; trades latency
 * for sensitivity to brief clicks. */
#define DSP_RMS_WINDOW_SAMPLES  16u

/* Onset: RMS must jump this much over a 4 ms baseline. Fixed-point (×256) of
 * the linear amplitude ratio, so no log/exp in the DSP loop. 18 dB ≈ 7.94× → 2033. */
#define DSP_ONSET_RATIO_X256    2033

/* Two-band gate: E(2-6 kHz)/E(<1 kHz) must exceed this. Same ×256. 2.0× → 512. */
#define DSP_BAND_RATIO_X256     512

/* Decay confirm: high-band energy must persist this many ms post-onset to be a
 * real impact (rejects single-sample clicks / static pops). DEFAULT, host can
 * override via CFG DECAY_CONFIRM_MS. Was 40 ms but that missed short strikes
 * (putts, wedge contact) and claps in bench testing; lowered to 5 ms. Runtime
 * bound 1..200 ms. 5 ms at 16 kHz = 80 samples. */
#define DSP_DECAY_CONFIRM_MS    5

/* Debounce after firing. 300 ms avoids double-firing on the ball bouncing off
 * the enclosure, yet still catches a quick re-tee. */
#define DSP_DEBOUNCE_MS         300u

/* IIR LPF coefficients (Q15) for the impact-detect band split.
 * alpha_q15 = round(alpha * 32768) at 16 kHz fs:
 *   fc = 1 kHz → 0.282 → 9240
 *   fc = 6 kHz → 0.702 → 23004 */
#define DSP_LPF_ALPHA_LO_Q15            9240
#define DSP_LPF_ALPHA_HI_Q15            23004

/* Baseline tracker: update interval × step shift = time constant. 64 samples
 * at 16 kHz, 1/16 step → ~64 ms TC, slow enough that an impact won't poison it. */
#define DSP_BASELINE_UPDATE_INTERVAL    64u
#define DSP_BASELINE_STEP_SHIFT         4

/* Onset jump on the squared high-band envelope: 18 dB ≈ 7.94× amplitude ≈ 63× energy. */
#define DSP_ONSET_JUMP_RATIO_SQUARED    63

/* --------------------------------------------------------- strobe defaults */

/* Boot defaults; host can override via CFG once USB CDC is up. Match the "fast"
 * pulse vector in Software/LMSourceCode/ImageProcessing/pulse_strobe.cpp so an
 * unconfigured stock Pico still does something useful on the bench. */

/* Pi-side fast-pulse default: 1 bit at 115200 baud = 8.68 µs. Host overrides
 * via CFG PULSE_WIDTH_US. Keeps unconfigured firmware aligned with the C++
 * path so a bench test produces a sane ghost pattern. */
#define STROBE_DEFAULT_PULSE_WIDTH_US  8.68f

/* 32 (up from 16) fits the longest putter pattern (5 base + 5 tail-repeat of
 * 444 ms) without host multi-FIRE orchestration. Worst case 256 bytes of
 * pattern RAM, trivially in BSS. */
#define STROBE_MAX_PULSES              32   /* hard cap — RAM and PIO FIFO depth */

/* Compiled-pattern capacity, in 32-bit PIO words. Each 524 us LOW gap (16-bit
 * count at 125 MHz) is one word; a longer train is rejected, never truncated.
 * Fits the putter strobe (5 pulses, 30-50 ms gaps, one external-trigger
 * exposure — legacy held the shutter open for the whole train, ~281 words)
 * with headroom. At ~524 us/word, 512 words bounds any compilable train to
 * ~270 ms, inside WATCHDOG_TIMEOUT_MS that strobe_fire's blocking DMA wait runs
 * against. 2 KB BSS. Shared so strobe.c, compiler, and host tests agree. */
#define STROBE_PATTERN_MAX_WORDS  512u

/* Per-interval upper bound. Longer gaps would push the DMA
 * wait_for_finish_blocking past the watchdog timeout and reset mid-train (PIO
 * keeps clocking after a watchdog POR-light reset until real POR gates the PIO
 * clock, leaving the LED on far too long). 1000 ms is above any real putter
 * interval; pathological values rejected silently. */
#define STROBE_MAX_INTERVAL_MS         1000.0f

/* Pete: boost converter droops significantly beyond 500 µs. Hard cap enforced
 * in strobe_set_pulse_train and the CFG parser. */
#define STROBE_MAX_PULSE_WIDTH_US 500.0f

/* Total on-time per train (pulse_width × count). Caps per-fire LED energy so
 * the host can't request an unsurvivable train. 5 ms at 13 A ≈ 65 mJ, inside
 * the Vishay VSMA1085400 pulse spec. Real shots run ≪ 100 µs total. */
#define STROBE_MAX_TRAIN_ON_TIME_US 5000.0f

/* Minimum gap between fires. Boost cap needs ~tens of ms to recharge after a
 * multi-pulse train; fires before recharge dim the image anyway. Host can lower
 * via CFG MIN_INTER_SHOT_MS for single-pulse calibration sweeps. */
#define STROBE_DEFAULT_MIN_INTER_SHOT_MS  200u

/* Hard floor for MIN_INTER_SHOT_MS — parser clamps up to this even on an
 * explicit 0, protecting the boost cap recharge time. 20 ms is below normal
 * calibration cadence (host single-pulse fires, camera-frame-rate limited). */
#define STROBE_MIN_INTER_SHOT_MS_FLOOR  20u

/* Pre-trigger delay before strobe_fire pulls camera XTR low. Mirrors
 * kPuttingStrobeDelayMs (50 ms for putts) — host "ball settle" pad without a
 * userspace usleep. Set via CFG PRE_TRIGGER_DELAY_MS. */
#define STROBE_DEFAULT_PRE_TRIGGER_DELAY_MS  0u

/* Arm-quiet gate: refuse CFG ARMED=1 if mic energy exceeds
 * mic_threshold / DSP_ARM_QUIET_FACTOR, so arming in the tail of a noisy event
 * (clap, dropped club) can't auto-trigger on the way down.
 *
 * UNITS: mic_threshold and impact_detect_current_rms() are mean-square (energy)
 * units, so this factor is an ENERGY ratio — factor 2 ≈ √2× amplitude headroom.
 * Doubling it halves the energy ceiling but only drops amplitude by ~√2.
 *
 * The arm-quiet ceiling is threshold/2: a ~2.5M putt floor leaves a ~1.25M ceiling
 * that still clears a ~1M room. Factor 4 gave only a 0.6M ceiling < the room, so a
 * low putt floor could never arm. */
#define DSP_ARM_QUIET_FACTOR  2

/* Hardware watchdog timeout. Covers the longest train (16 pulses × 7 ms ≈
 * 110 ms) plus a slow USB flush. On timeout the silicon resets with the strobe
 * pin LOW (gpio_init default). */
#define WATCHDOG_TIMEOUT_MS  2000u

/* Max PIN_STROBE_OUT HIGH hold on CFG STROBE_HOLD=1 before auto-release. The
 * VSMA1085400 IR LEDs are pulse-rated (~5 A peak / 1 % duty) and overheat under
 * sustained 10+ A DC beyond ~hundreds of ms. 200 ms exceeds any single ADC read
 * in the Pi calibration sweep but bounds damage if the host crashes mid-sweep.
 * Host extends by re-issuing CFG STROBE_HOLD=1 before this elapses. */
#define STROBE_MAX_HOLD_MS  200u

/* Firmware version — surfaced on the boot LOG line. */
#define PITRAC_PICO_FW_VERSION "0.8.1"

/* EVENT RMS streaming rate ceiling (samples/sec). Caps the web UI mic
 * visualiser so USB CDC TX can't saturate and starve STATUS replies. */
#define STREAM_RMS_MAX_HZ  500u

/* Cam2/Cam1 XTR setup time before the strobe train. Host-tunable via CFG
 * CAM_XTR_SETUP_US. IMX296/Mira220 imply a few hundred µs min; 1 ms is safe. */
#define CAM_XTR_DEFAULT_SETUP_US 1000

/* Default intervals (ms) — gap after pulse N (LOW time before the next),
 * matching the kFastPulseIntervals vector in the C++ pipeline. */
#define STROBE_DEFAULT_INTERVALS_MS    { 0.7f, 1.8f, 3.0f, 2.2f, 3.0f, 7.1f, 4.0f, 0.0f }
#define STROBE_DEFAULT_INTERVAL_COUNT  8

/* Default impact-detect threshold: raw ADC counts after the impact_detect.c
 * fixed-point scaling; needs empirical tuning per install. 4096 is mid-range
 * for the 18-bit SPH0645 mantissa. */
#define DSP_DEFAULT_THRESHOLD          4096

/* ------------------------------------------------------ PIO / DMA assignments */

/* I2S RX on pio0, strobe on pio1 — separate blocks so they never contend for
 * instruction memory. (pio0 is closer to the GPIO pads, marginally better
 * setup/hold.) */
#define I2S_PIO          pio0
#define I2S_PIO_SM       0

#define STROBE_PIO       pio1
#define STROBE_PIO_SM    0

/* DMA: chan 0 = I2S RX (free-running, self-chained), chan 1 = strobe TX
 * (one-shot on FIRE). Channels are interchangeable on RP2040. */
#define I2S_DMA_CHAN     0
#define STROBE_DMA_CHAN  1

/* ---------------------------------------------------------- ipc / multicore */

/* Multicore mailbox sentinels passed via multicore_fifo (uint32_t). core0
 * (DSP) posts MAILBOX_STRIKE on detection; core1 (USB) emits the EVENT line. */
#define MAILBOX_STRIKE             0xA110C001u   /* "atomic-ish strike" */
#define MAILBOX_MANUAL_FIRE        0xA110C002u
#define MAILBOX_DISARM_AFTER_FIRE  0xA110C004u   /* core0 → core1: log auto-disarm */
#define MAILBOX_DISARM_TIMEOUT     0xA110C005u   /* core0 → core1: log arm-timeout */
#define MAILBOX_FIRE_REFUSED_COOLDOWN  0xA110C006u  /* core0 → core1: log cooldown reject */
#define MAILBOX_MANUAL_FIRE_DONE   0xA110C008u   /* core0 → core1: manual fire completed, emit EVENT */
#define MAILBOX_FIRE_REFUSED_HELD  0xA110C009u   /* core0 → core1: log strobe-held refusal */
#define MAILBOX_HARDWARE_FIRE      0xA110C00Au   /* core0 <- gp9 irq: hardware fire requested */
#define MAILBOX_HARDWARE_FIRE_DONE 0xA110C00Bu   /* core0 -> core1: emit EVENT HARDWARE_FIRE */
#define MAILBOX_MANUAL_FIRE_PEAK   0xA110C00Cu   /* core1 -> core0: fire + sample ADC0 peak */
#define MAILBOX_MANUAL_FIRE_PEAK_DONE 0xA110C00Du /* core0 -> core1: emit EVENT PEAK adc=N samples=M */

/* USB CDC line accumulator, sized for the worst-case CFG PULSE_INTERVALS line
 * (STROBE_MAX_PULSES floats × ~10 chars + prefix). Overflow LOGs once, drops
 * the rest of the line. */
#define USB_CDC_LINE_BUFFER_SIZE       512u

#endif /* PITRAC_PICO_CONFIG_H */
