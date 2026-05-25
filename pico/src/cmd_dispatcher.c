#include "cmd_dispatcher.h"

#include <stddef.h>

#include "config.h"

void cmd_dispatcher_apply(const pitrac_cmd_t *c, const hw_driver_t *hw) {
    switch (c->kind) {

    case CMD_CFG_THRESHOLD:
        hw->set_threshold(c->u.threshold);
        hw->emit_log("threshold updated");
        break;

    case CMD_CFG_INTERVALS:
        if (!hw->set_pulse_train(c->u.intervals.intervals_ms,
                                 c->u.intervals.count,
                                 0.0f /* sentinel: keep current pulse width */)) {
            hw->emit_log("error: interval set rejected (bad count or overflow)");
        } else {
            hw->emit_log("intervals updated");
        }
        break;

    case CMD_CFG_PULSE_WIDTH:
        if (!hw->set_pulse_train(NULL, 0, c->u.pulse_width_us)) {
            hw->emit_log("error: pulse width set rejected");
        } else {
            hw->emit_log("pulse_width updated");
        }
        break;

    case CMD_CFG_ARMED:
        if (c->u.armed) {
            /* Refuse arm if the decay tail of a recent noise event would
             * re-trigger us immediately. */
            int32_t now_rms = hw->current_rms();
            int32_t quiet_ceiling = hw->get_threshold() / DSP_ARM_QUIET_FACTOR;
            if (now_rms > quiet_ceiling) {
                hw->emit_log("error: arm refused (room too loud, retry when quiet)");
                break;
            }
        }
        hw->set_armed(c->u.armed);
        hw->emit_log(c->u.armed ? "armed" : "disarmed");
        break;

    case CMD_CFG_ARM_TIMEOUT:
        hw->set_arm_timeout(c->u.u32);
        hw->emit_log("arm_timeout updated");
        break;

    case CMD_CFG_CAM_XTR_SETUP:
        hw->set_cam_xtr_setup(c->u.u32);
        hw->emit_log("cam_xtr_setup updated");
        break;

    case CMD_CFG_MIN_INTER_SHOT:
        hw->set_min_inter_shot(c->u.u32);
        hw->emit_log("min_inter_shot updated");
        break;

    case CMD_CFG_PRE_TRIGGER_DELAY:
        hw->set_pre_trigger_delay(c->u.u32);
        hw->emit_log("pre_trigger_delay updated");
        break;

    case CMD_CFG_DECAY_CONFIRM:
        hw->set_decay_confirm(c->u.u32);
        hw->emit_log("decay_confirm updated");
        break;

    case CMD_CFG_STROBE_HOLD:
        if (c->u.armed) {
            if (hw->hold_assert()) {
                hw->emit_log("strobe_hold=1 (200ms safety timeout)");
            } else {
                hw->emit_log("error: strobe_hold rejected (fire in flight?)");
            }
        } else {
            hw->hold_release();
            hw->emit_log("strobe_hold=0");
        }
        break;

    case CMD_CAM_PULSE:
        hw->cam_pulse(c->u.u32);
        break;

    case CMD_FIRE:
        hw->request_manual_fire();
        break;

    case CMD_STATUS:
        hw->emit_status();
        break;

    case CMD_RESET:
        hw->emit_log("resetting");
        hw->request_reset();
        break;

    case CMD_BOOTSEL:
        hw->emit_log("entering BOOTSEL mode in 100ms...");
        hw->request_bootsel();
        break;

    case CMD_SELFTEST:
        hw->selftest();
        break;

    case CMD_NONE:
    case CMD_INVALID:
    default:
        hw->emit_log("error: invalid command");
        break;
    }
}
