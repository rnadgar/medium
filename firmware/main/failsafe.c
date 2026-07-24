/*
 * failsafe.c — over-pressure fail-open state machine. NO ESP-IDF INCLUDES.
 * See failsafe.h for the contract and docs/architecture.md for the spec.
 */
#include "failsafe.h"

#include <stddef.h>
#include <string.h>

failsafe_config_t failsafe_default_config(void)
{
    failsafe_config_t cfg = {
        .threshold_pa = 150,
        .clear_hysteresis_pa = 30,
        .auto_clear_enable = true,
        .trip_confirm_samples = 2,
        .clear_confirm_samples = 3,
        .arm_pressure_pa = 15,
        .idle_pressure_pa = 5,
        .idle_disarm_ms = 60000,
    };
    return cfg;
}

void failsafe_init(failsafe_t *fs, const failsafe_config_t *cfg)
{
    if (fs == NULL) {
        return;
    }
    memset(fs, 0, sizeof(*fs));
    fs->cfg = (cfg != NULL) ? *cfg : failsafe_default_config();
    fs->state = FAILSAFE_STATE_IDLE;
}

static uint16_t clear_level_pa(const failsafe_t *fs)
{
    uint16_t thr = fs->cfg.threshold_pa;
    uint16_t hyst = fs->cfg.clear_hysteresis_pa;
    return (hyst >= thr) ? 0 : (uint16_t)(thr - hyst);
}

/* Enter TRIPPED: fail open and flag reports. */
static void do_trip(failsafe_t *fs, uint16_t pressure_pa, failsafe_action_t *act)
{
    fs->state = FAILSAFE_STATE_TRIPPED;
    fs->latched = false;
    fs->trip_count = 0;
    fs->clear_count = 0;
    fs->last_trip_pressure_pa = pressure_pa;
    if (!fs->tripped) {
        fs->tripped = true;
        act->tripped_changed = true;
    }
    act->request_open = true;
}

static void track_idle_disarm(failsafe_t *fs, uint16_t pressure_pa, uint32_t now_ms)
{
    if (pressure_pa <= fs->cfg.idle_pressure_pa) {
        if (!fs->low_since_valid) {
            fs->low_since_valid = true;
            fs->low_since_ms = now_ms;
        } else if ((uint32_t)(now_ms - fs->low_since_ms) >= fs->cfg.idle_disarm_ms) {
            fs->state = FAILSAFE_STATE_IDLE;
            fs->trip_count = 0;
        }
    } else {
        fs->low_since_valid = false;
    }
}

failsafe_action_t failsafe_sample(failsafe_t *fs, uint16_t pressure_pa,
                                  uint32_t now_ms)
{
    failsafe_action_t act = {0};
    if (fs == NULL) {
        return act;
    }

    /* A good sample clears a previously latched sensor fault. Motor faults
     * (1/2) are NOT cleared by pressure data. */
    if (fs->fault_code == FAILSAFE_FAULT_SENSOR) {
        fs->fault_code = FAILSAFE_FAULT_NONE;
        act.fault_changed = true;
    }

    switch (fs->state) {
    case FAILSAFE_STATE_IDLE:
        if (pressure_pa >= fs->cfg.arm_pressure_pa) {
            fs->state = FAILSAFE_STATE_ARMED;
            fs->ever_armed = true;
            fs->trip_count = 0;
            fs->low_since_valid = false;
            /* Fall through to ARMED handling for this same sample so a hard
             * blower start still needs only trip_confirm_samples total. */
        } else {
            break;
        }
        /* fallthrough */
    case FAILSAFE_STATE_ARMED:
        if (pressure_pa > fs->cfg.threshold_pa) {
            fs->trip_count++;
            if (fs->trip_count >= fs->cfg.trip_confirm_samples) {
                do_trip(fs, pressure_pa, &act);
            }
        } else {
            fs->trip_count = 0;
            track_idle_disarm(fs, pressure_pa, now_ms);
        }
        break;

    case FAILSAFE_STATE_TRIPPED:
        if (fs->latched) {
            /* Recovered but auto-clear disabled: hold fail-open until a
             * manual clear from HA. Nothing to do per-sample. */
            break;
        }
        if (pressure_pa < clear_level_pa(fs)) {
            fs->clear_count++;
            if (fs->clear_count >= fs->cfg.clear_confirm_samples) {
                fs->state = FAILSAFE_STATE_RECOVERING;
                fs->clear_count = 0;
                if (fs->cfg.auto_clear_enable) {
                    /* Resume commanded position; back to ARMED if the blower
                     * is still running, IDLE otherwise. */
                    fs->tripped = false;
                    act.tripped_changed = true;
                    act.request_resume = true;
                    fs->low_since_valid = false;
                    fs->state = (pressure_pa >= fs->cfg.arm_pressure_pa)
                                    ? FAILSAFE_STATE_ARMED
                                    : FAILSAFE_STATE_IDLE;
                } else {
                    /* Latch: stay TRIPPED (fail-open held, attr stays true)
                     * until HA clears. */
                    fs->latched = true;
                    fs->state = FAILSAFE_STATE_TRIPPED;
                }
            }
        } else {
            fs->clear_count = 0;
        }
        break;

    case FAILSAFE_STATE_RECOVERING:
        /* Transient only — resolved within the sample that entered it. */
        fs->state = FAILSAFE_STATE_IDLE;
        break;
    }

    return act;
}

failsafe_action_t failsafe_sensor_fault(failsafe_t *fs, uint32_t now_ms)
{
    (void)now_ms;
    failsafe_action_t act = {0};
    if (fs == NULL) {
        return act;
    }
    if (fs->fault_code != FAILSAFE_FAULT_SENSOR) {
        fs->fault_code = FAILSAFE_FAULT_SENSOR;
        act.fault_changed = true;
    }
    /* Without pressure data the fail-safe is blind: open as a precaution
     * (docs/architecture.md failure-modes table). Treated as a trip so the
     * fail-open state is visible in attr 0x0000. */
    if (!fs->tripped) {
        fs->tripped = true;
        act.tripped_changed = true;
        act.request_open = true;
        fs->state = FAILSAFE_STATE_TRIPPED;
        fs->latched = false;
        fs->trip_count = 0;
        fs->clear_count = 0;
    }
    return act;
}

failsafe_action_t failsafe_set_motor_fault(failsafe_t *fs, failsafe_fault_t code)
{
    failsafe_action_t act = {0};
    if (fs == NULL) {
        return act;
    }
    if (fs->fault_code != (uint8_t)code) {
        fs->fault_code = (uint8_t)code;
        act.fault_changed = true;
    }
    return act;
}

failsafe_action_t failsafe_manual_clear(failsafe_t *fs)
{
    failsafe_action_t act = {0};
    if (fs == NULL) {
        return act;
    }
    if (fs->tripped) {
        fs->tripped = false;
        fs->latched = false;
        fs->clear_count = 0;
        fs->trip_count = 0;
        fs->state = FAILSAFE_STATE_IDLE;
        act.tripped_changed = true;
        act.request_resume = true;
    }
    return act;
}

void failsafe_set_threshold(failsafe_t *fs, uint16_t threshold_pa)
{
    if (fs != NULL) {
        fs->cfg.threshold_pa = threshold_pa;
    }
}

void failsafe_set_hysteresis(failsafe_t *fs, uint16_t hysteresis_pa)
{
    if (fs != NULL) {
        fs->cfg.clear_hysteresis_pa = hysteresis_pa;
    }
}

void failsafe_set_auto_clear(failsafe_t *fs, bool enable)
{
    if (fs != NULL) {
        fs->cfg.auto_clear_enable = enable;
    }
}

failsafe_sample_class_t failsafe_sample_class(const failsafe_t *fs)
{
    if (fs == NULL) {
        return FAILSAFE_SAMPLE_BASE;
    }
    switch (fs->state) {
    case FAILSAFE_STATE_ARMED:
    case FAILSAFE_STATE_TRIPPED:
    case FAILSAFE_STATE_RECOVERING:
        return FAILSAFE_SAMPLE_ACTIVE;
    case FAILSAFE_STATE_IDLE:
    default:
        /* Fresh boot: blower state unknown -> base cadence. Once we have
         * seen the blower and confirmed it off -> slow idle cadence. */
        return fs->ever_armed ? FAILSAFE_SAMPLE_IDLE : FAILSAFE_SAMPLE_BASE;
    }
}
