/*
 * config.h — pin assignments, sample rates, and tunable defaults for the
 * PiTrac Pico firmware. Keep this header header-only: everything here is a
 * compile-time constant so the compiler can fold pin numbers and clock
 * divisors into immediates.
 *
 * Hardware target: original Raspberry Pi Pico (RP2040), driving:
 *   - SPH0645 I2S MEMS mic (Adafruit 3421)
 *   - MCP1407 gate driver feeding a MOSFET → IR LED bank
 *   - Cam2 external trigger (IMX296 / Mira220), active low
 *
 * The pin numbers below match the wiring documented in README.md and were
 * picked so the I2S signals stay together on one PIO state machine's
 * consecutive pins (RP2040 PIO can only address contiguous pin ranges for the
 * sideset + OUT/IN groups it manages).
 */

#ifndef PITRAC_PICO_CONFIG_H
#define PITRAC_PICO_CONFIG_H

#include <stdint.h>

/* ---------------------------------------------------------------- pins ---- */

/* SPH0645 I2S MEMS mic. We're the master — Pico drives BCLK + LRCLK.
 *
 * The SPH0645 needs BCLK and LRCLK on adjacent GPIOs because PIO sideset
 * controls a contiguous pin span: we sideset BCLK and LRCLK from one state
 * machine while sampling DOUT via IN. GP10/GP11/GP12 keeps everything on
 * the same physical side of the Pico and lines up with the bottom row in
 * stock SPH0645 breakouts. */
#define PIN_I2S_BCLK   10  /* SCK on most SPH0645 breakouts */
#define PIN_I2S_LRCLK  11  /* WS / LRC / "select" */
#define PIN_I2S_DIN    12  /* SD / DOUT from mic */

/* Strobe output → drives MCP1407 input on the V3 connector board.
 * Driven by ir_strobe.pio at cycle-accurate timing. */
#define PIN_STROBE_OUT 13

/* Cam2 external trigger. IMX296/Mira220 expect this active low: pulled low
 * to start exposure, released high to end it. We just toggle it from C — it
 * doesn't need cycle-accurate timing relative to the strobe; the camera's
 * own shutter latency dominates. */
#define PIN_CAM2_XTR   15

/* M2 pins. Pi side: GPIO 26 -> GP9 (FIRE_IN), GP7 -> Pi GPIO 27 (IRQ_OUT),
 * GP8 -> Pi GPIO 22 (HEARTBEAT_OUT). FIRE_IN uses internal pull-down so a
 * disconnected wire reads LOW and never triggers. */
#define PIN_FIRE_IN        9
#define PIN_IRQ_OUT        7
#define PIN_HEARTBEAT_OUT  8

/* IRQ_OUT pulse width on STRIKE / HARDWARE_FIRE. Pi-side has to sample with
 * an IRQ-on-rising-edge handler; 100 us is comfortably above any reasonable
 * polling interval and well under the 200 ms cooldown. */
#define IRQ_OUT_PULSE_US   100u

/* Cam1 external trigger — reserved for future stereo capture support. Same
 * polarity convention as cam2. Driven HIGH at boot; strobe_fire() pulses both
 * cam pins together so existing single-camera installs see no change. */
#define PIN_CAM1_XTR   14

/* Onboard LED — handy heartbeat / armed-state indicator.
 * GP25 is the default on a stock Pico board. */
#define PIN_STATUS_LED 25

/* RP2040 internal-function pins (per Pico board datasheet). These are
 * built into the Pico module's PCB, not user-routable to other peripherals,
 * but we read/write them via the regular GPIO API. */

/* VBUS sense — HIGH if 5 V is present on the micro-USB connector. Lets the
 * Pico tell whether the Pi pulled USB power (or whether someone yanked the
 * cable). Pure digital read, no ADC needed. */
#define PIN_VBUS_SENSE   24

/* SMPS power-save mode control. LOW = PFM (default — efficient at low loads
 * but adds ripple in audible range). HIGH = forced PWM (less efficient, much
 * quieter — better for clean ADC reads of VSYS and for the I2S mic). We hold
 * this HIGH all the time; the few mA penalty is well worth the noise
 * reduction in a USB-powered build. */
#define PIN_SMPS_PSM     23

/* VSYS divided by 3 is connected to ADC3 (GPIO 29). Multiply ADC mV reading
 * by 3 to recover the real supply voltage. Used for brownout detection. */
#define ADC_VSYS_CHANNEL 3

/* V3 board CUR-SENSE (test point TP4) is wired to Pico GPIO 26 = ADC channel 0.
 * Direct input, no divider — shunt sees 0 V at idle and ~1 V at 10 A peak,
 * well within ADC range. Used for LED current calibration sampling during
 * strobe_fire_peak(). */
#define PIN_CUR_SENSE_ADC      26
#define ADC_CUR_SENSE_CHANNEL  0

/* ----------------------------------------------------------- audio path --- */

/* Source sample rate. SPH0645 datasheet says it tolerates 32–64 fs on BCLK;
 * we run 48 kHz × 64 = 3.072 MHz BCLK so each LRCLK frame is exactly 64 BCLK
 * cycles — one 32-bit slot per channel. The SPH0645 itself only outputs on
 * one channel slot (selected by SEL pin; SEL=GND → left). */
#define I2S_SAMPLE_RATE_HZ      48000u
#define I2S_BITS_PER_SAMPLE     32u    /* slot width — actual data is 18 bits MSB */
#define I2S_BCLK_FREQ_HZ        (I2S_SAMPLE_RATE_HZ * I2S_BITS_PER_SAMPLE * 2)  /* L+R slots */

/* DSP decimation factor. 48 kHz → 16 kHz keeps the band of interest (2-6 kHz)
 * comfortably below Nyquist and cuts ring-buffer RAM by 3×. A simple 3-tap
 * average is enough anti-aliasing for a detection algorithm — we're not
 * recording audio. */
#define DSP_DECIMATION          3u
#define DSP_SAMPLE_RATE_HZ      (I2S_SAMPLE_RATE_HZ / DSP_DECIMATION)  /* 16000 Hz */

/* Ring buffer for raw I2S samples, in 32-bit slots. Sized for ~32 ms of audio
 * at 48 kHz — way more than the DSP needs but gives the USB worker plenty of
 * slack in case the Pi yanks scheduler time from us. Must be a power of two
 * (the SPSC ring buffer uses bitmasking, not modulo). */
#define I2S_RING_LOG2           11
#define I2S_RING_SAMPLES        (1u << I2S_RING_LOG2)  /* 2048 samples ≈ 42 ms @ 48 kHz */

/* RMS window: 1 ms at 16 kHz = 16 samples. Short window = fast onset
 * detection; bias trades latency for sensitivity to brief clicks. */
#define DSP_RMS_WINDOW_SAMPLES  16u

/* Onset detection: require RMS to jump this many dB over a 4 ms baseline.
 * Stored as a fixed-point scalar (×256) of the linear amplitude ratio so we
 * never call log()/exp() in the DSP loop. 18 dB ≈ 7.94× linear → 7.94×256 ≈ 2033. */
#define DSP_ONSET_RATIO_X256    2033

/* Two-band ratio threshold: E(2-6 kHz) / E(<1 kHz) must exceed this to count.
 * Same fixed-point convention. 2.0× → 512. */
#define DSP_BAND_RATIO_X256     512

/* Decay confirmation: high-band energy must persist this many ms post-onset
 * before we believe it was a real impact (rejects single-sample clicks /
 * static pops). This is a DEFAULT — host can override at runtime via
 * `CFG DECAY_CONFIRM_MS=<n>`. 40 ms was the original value; lowered to 5 ms
 * since real golf-ball strikes can be quite short (especially putts and
 * wedge contact) and 40 ms missed claps in bench testing. Bounded at runtime
 * to 1..200 ms. 5 ms at 16 kHz = 80 samples. */
#define DSP_DECAY_CONFIRM_MS    5

/* Debounce: ignore further triggers for this long after firing. 300 ms is
 * long enough that we won't double-fire on the ball's bounce off the
 * enclosure walls but short enough that we'll catch a quick re-tee. */
#define DSP_DEBOUNCE_MS         300u

/* IIR low-pass coefficients (Q15) for the band-pass split used in impact
 * detection. alpha_q15 = round(alpha * 32768). At 16 kHz fs:
 *   fc = 1 kHz → alpha ≈ 0.282 → 9240
 *   fc = 6 kHz → alpha ≈ 0.702 → 23004 */
#define DSP_LPF_ALPHA_LO_Q15            9240
#define DSP_LPF_ALPHA_HI_Q15            23004

/* Baseline tracker decay. Samples between updates × step shift = effective
 * time constant. 64 samples at 16 kHz, 1/16 step → ~64 ms TC, slow enough
 * that a real impact won't poison the baseline. */
#define DSP_BASELINE_UPDATE_INTERVAL    64u
#define DSP_BASELINE_STEP_SHIFT         4

/* Onset jump threshold on the squared high-band envelope: 18 dB amplitude
 * jump ≈ 7.94× linear ≈ 63× in energy. */
#define DSP_ONSET_JUMP_RATIO_SQUARED    63

/* --------------------------------------------------------- strobe defaults */

/* These are the firmware boot defaults — the host can override any of them
 * via CFG commands once USB CDC is up. They match the "fast" pulse vector in
 * Software/LMSourceCode/ImageProcessing/pulse_strobe.cpp so a stock Pico that
 * never gets configured still does something useful in bench testing. */

/* Match the Pi-side fast-pulse default: 1 bit at 115200 baud = 8.68 µs.
 * Host can override via CFG PULSE_WIDTH_US once it connects. The matching
 * default keeps stock-firmware behavior aligned with the existing C++ code
 * path so a benchtop test without host CFG still produces a sane ghost
 * pattern. */
#define STROBE_DEFAULT_PULSE_WIDTH_US  8.68f

/* Bumped from 16 to 32 to fit the longest putter pattern (5 base pulses +
 * 5 tail-repeat of 444 ms) without forcing the host into multi-FIRE
 * orchestration. 32 × 4 bytes/word × max-2 continuation words = 256 bytes
 * of pattern RAM at worst, trivially in BSS. */
#define STROBE_MAX_PULSES              32   /* hard cap — RAM and PIO FIFO depth */

/* Per-interval upper bound. Host can request gaps up to this long; anything
 * longer would push the DMA `wait_for_finish_blocking` past the watchdog
 * timeout and reset the Pico mid-train (PIO keeps clocking after a watchdog
 * POR-light reset until the chip's actual POR gates the PIO clock, leaving
 * the LED on far longer than intended). 1000 ms is well above any real
 * putter interval; rejects pathological values silently. */
#define STROBE_MAX_INTERVAL_MS         1000.0f

/* Pete: boost converter starts to droop significantly beyond 500 µs.
 * Hard cap enforced in strobe_set_pulse_train and the CFG parser. */
#define STROBE_MAX_PULSE_WIDTH_US 500.0f

/* Total on-time across one pulse train (pulse_width × count). Caps the energy
 * the LED bank can dump per fire so the host can't request an unsurvivable
 * train via CFG. 5 ms at 13 A ≈ 65 mJ — well inside Vishay VSMA1085400 pulse
 * spec. Real shots run ≪ 100 µs total. */
#define STROBE_MAX_TRAIN_ON_TIME_US 5000.0f

/* Minimum interval between consecutive fires. Protects the boost converter
 * cap and the LED thermal mass — the cap needs ~tens of ms to recharge fully
 * after a multi-pulse train, and back-to-back fires before recharge would
 * give dim images anyway. Host can lower via CFG MIN_INTER_SHOT_MS for
 * calibration sweeps where each fire is a tiny single pulse. */
#define STROBE_DEFAULT_MIN_INTER_SHOT_MS  200u

/* Hard floor for MIN_INTER_SHOT_MS — the parser clamps any lower request up
 * to this value, even if the host explicitly asks for 0. Protects the boost
 * converter cap from back-to-back fires that exceed its recharge time. 20 ms
 * is well below normal calibration-sweep cadence (which is host-side
 * single-pulse fires limited by camera frame rate anyway). */
#define STROBE_MIN_INTER_SHOT_MS_FLOOR  20u

/* Pre-trigger delay applied before strobe_fire kicks the camera XTR low.
 * Mirrors kPuttingStrobeDelayMs in the C++ pipeline (50 ms for putts) —
 * lets the host pad a deliberate "ball settle" pause without burning a
 * userspace usleep. Host sets via CFG PRE_TRIGGER_DELAY_MS. */
#define STROBE_DEFAULT_PRE_TRIGGER_DELAY_MS  0u

/* Arm-quiet gate: refuse CFG ARMED=1 if the current mic energy is louder
 * than (mic_threshold / DSP_ARM_QUIET_FACTOR). Prevents arming during the
 * tail of a noisy event (clap, dropped club, throat clear) that would
 * auto-trigger the detector on its way down.
 *
 * UNITS: both mic_threshold and impact_detect_current_rms() are in
 * mean-square (energy) units — sum-of-squares / window. So the factor here
 * is an ENERGY ratio. In amplitude terms a factor of 4 corresponds to
 * ~2× amplitude headroom below the detection threshold. Keep that in mind
 * if you ever change this — doubling the factor halves the energy ceiling
 * but only drops amplitude by ~√2. */
#define DSP_ARM_QUIET_FACTOR  4

/* Hardware watchdog timeout. Generous enough to cover the longest strobe
 * train (max 16 pulses × 7 ms gap ≈ 110 ms) plus a slow USB flush. If
 * watchdog_update() doesn't get called for this long, the silicon resets
 * itself with the strobe pin pulled LOW by gpio_init's default state. */
#define WATCHDOG_TIMEOUT_MS  2000u

/* Max time the Pico will hold PIN_STROBE_OUT HIGH on a `CFG STROBE_HOLD=1`
 * before auto-releasing for safety. The VSMA1085400 IR LEDs are pulse-rated
 * (~5 A peak / 1 % duty for ms-scale pulses) and would exceed their thermal
 * envelope under sustained 10+ A DC for more than ~hundreds of ms. 200 ms
 * is far longer than any individual ADC read in the Pi-side calibration
 * sweep but short enough to bound damage if the host crashes mid-sweep.
 * Host can extend by re-issuing CFG STROBE_HOLD=1 before this elapses. */
#define STROBE_MAX_HOLD_MS  200u

/* Firmware version string — surfaced on the boot LOG line. */
#define PITRAC_PICO_FW_VERSION "0.6.1"

/* Upper bound on the EVENT RMS streaming rate (samples per second). The web
 * UI's mic visualiser doesn't need anything faster than this; capping it keeps
 * the USB CDC TX queue from saturating and starving STATUS replies. */
#define STREAM_RMS_MAX_HZ  100u

/* Cam2 / Cam1 XTR setup time before the strobe train kicks off.
 * Host-tunable via CFG CAM_XTR_SETUP_US. IMX296/Mira220 datasheets imply
 * a few hundred µs minimum; 1 ms is a safe default. */
#define CAM_XTR_DEFAULT_SETUP_US 1000

/* Default intervals (ms) — each entry is the gap after pulse N (LOW time
 * before the next pulse), matching the kFastPulseIntervals vector in the
 * main C++ pipeline. */
#define STROBE_DEFAULT_INTERVALS_MS    { 0.7f, 1.8f, 3.0f, 2.2f, 3.0f, 7.1f, 4.0f, 0.0f }
#define STROBE_DEFAULT_INTERVAL_COUNT  8

/* Default impact-detect threshold. This is raw ADC counts after the
 * fixed-point scaling in impact_detect.c — needs empirical tuning on first
 * install. 4096 is mid-range for the 18-bit SPH0645 mantissa we keep. */
#define DSP_DEFAULT_THRESHOLD          4096

/* ------------------------------------------------------ PIO / DMA assignments */

/* We have two PIO blocks (pio0, pio1) and four state machines each. We park
 * the I2S RX on pio0 (closer to the GPIO pads on the silicon — slightly
 * better setup/hold margin in our timing closure) and the strobe on pio1 so
 * the two never contend for instruction memory. */
#define I2S_PIO          pio0
#define I2S_PIO_SM       0

#define STROBE_PIO       pio1
#define STROBE_PIO_SM    0

/* DMA channels: 0 for I2S RX (free-running, chained back to itself), 1 for
 * strobe TX (one-shot, started on FIRE). Both are arbitrary — DMA channels
 * on RP2040 are interchangeable for our access patterns. */
#define I2S_DMA_CHAN     0
#define STROBE_DMA_CHAN  1

/* ---------------------------------------------------------- ipc / multicore */

/* Multicore mailbox sentinel values. core0 (DSP) posts MAILBOX_STRIKE on
 * detection so core1 (USB) can emit an EVENT line. Plain ints — the SDK's
 * multicore_fifo_push_blocking takes uint32_t. */
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

/* USB CDC line accumulator. Sized for the worst-case CFG PULSE_INTERVALS line
 * (STROBE_MAX_PULSES floats × ~10 chars each + prefix). Overflow LOGs once
 * and drops the rest of the line. */
#define USB_CDC_LINE_BUFFER_SIZE       512u

#endif /* PITRAC_PICO_CONFIG_H */
