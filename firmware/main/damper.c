/*
 * damper.c — pure damper position logic. NO ESP-IDF INCLUDES.
 * See damper.h for conventions (open_pct internal, lift at ZCL boundary,
 * steps: 0 = closed .. range_steps = open).
 */
#include "damper.h"

#include <stddef.h>

uint8_t damper_clamp_pct(int32_t pct)
{
    if (pct < 0) {
        return 0;
    }
    if (pct > (int32_t)DAMPER_PCT_MAX) {
        return DAMPER_PCT_MAX;
    }
    return (uint8_t)pct;
}

uint8_t damper_open_to_lift(uint8_t open_pct)
{
    return (uint8_t)(DAMPER_PCT_MAX - damper_clamp_pct(open_pct));
}

uint8_t damper_lift_to_open(uint8_t lift_pct)
{
    return (uint8_t)(DAMPER_PCT_MAX - damper_clamp_pct(lift_pct));
}

int32_t damper_pct_to_steps(uint8_t open_pct, int32_t range_steps)
{
    if (range_steps <= 0) {
        return 0;
    }
    uint8_t pct = damper_clamp_pct(open_pct);
    /* Round to nearest step. range_steps is bounded (< ~20k for this
     * mechanism) so 64-bit intermediates are ample. */
    int64_t num = (int64_t)range_steps * pct + (DAMPER_PCT_MAX / 2);
    return (int32_t)(num / DAMPER_PCT_MAX);
}

uint8_t damper_steps_to_pct(int32_t steps, int32_t range_steps)
{
    if (range_steps <= 0) {
        return 0;
    }
    if (steps <= 0) {
        return 0;
    }
    if (steps >= range_steps) {
        return DAMPER_PCT_MAX;
    }
    int64_t num = (int64_t)steps * DAMPER_PCT_MAX + range_steps / 2;
    return damper_clamp_pct((int32_t)(num / range_steps));
}

uint8_t damper_open_pct(const damper_t *d)
{
    if (d == NULL || !d->position_valid) {
        return 0;
    }
    return damper_steps_to_pct(d->position_steps, d->range_steps);
}

bool damper_plan_move(const damper_t *d, uint8_t target_open_pct,
                      damper_move_plan_t *plan)
{
    if (plan == NULL) {
        return false;
    }
    plan->direction = 0;
    plan->steps = 0;
    plan->target_steps = 0;
    plan->watchdog_steps = 0;

    if (d == NULL || !d->position_valid || d->range_steps <= 0) {
        return false; /* must home/calibrate first */
    }

    int32_t target = damper_pct_to_steps(target_open_pct, d->range_steps);
    int32_t delta = target - d->position_steps;

    plan->target_steps = target;
    if (delta > 0) {
        plan->direction = 1;
        plan->steps = delta;
    } else if (delta < 0) {
        plan->direction = -1;
        plan->steps = -delta;
    }
    /* Stall watchdog: expected steps x 1.25, rounded up
     * (docs/architecture.md "Stall/timeout"). */
    plan->watchdog_steps = (int32_t)(((int64_t)plan->steps * 5 + 3) / 4);
    return true;
}

void damper_apply_steps(damper_t *d, int8_t direction, int32_t steps_taken)
{
    if (d == NULL || steps_taken < 0) {
        return;
    }
    int64_t pos = d->position_steps;
    if (direction > 0) {
        pos += steps_taken;
    } else if (direction < 0) {
        pos -= steps_taken;
    }
    if (pos < 0) {
        pos = 0;
    }
    if (d->range_steps > 0 && pos > d->range_steps) {
        pos = d->range_steps;
    }
    d->position_steps = (int32_t)pos;
}
