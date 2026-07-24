/*
 * damper.h — pure damper position logic for the smart register.
 *
 * PURE LOGIC MODULE: no ESP-IDF includes, compiles on host (see
 * firmware/test/host). All hardware access lives in stepper.c.
 *
 * Position convention (docs/architecture.md, "the one inversion, stated
 * once"): firmware-internal position is open_pct where 100 = fully open
 * (max airflow). ZCL Window Covering lift percentage is the opposite
 * (0 = open, 100 = closed). The conversion lives HERE and nowhere else:
 *
 *     lift_pct = 100 - open_pct
 *
 * Step-space convention: 0 steps = fully CLOSED (homing datum at the
 * CLOSED limit switch), range_steps = fully OPEN.
 */
#ifndef SMART_REGISTER_DAMPER_H
#define SMART_REGISTER_DAMPER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAMPER_PCT_MAX 100U

/* Damper position state (pure data, owned by the motion controller). */
typedef struct {
    int32_t range_steps;    /* calibrated closed->open travel, > 0 when valid */
    int32_t position_steps; /* current position, 0 (closed) .. range_steps (open) */
    bool position_valid;    /* false until homed (or restored from clean NVS) */
} damper_t;

/* A planned move, produced by damper_plan_move(). */
typedef struct {
    int8_t direction;       /* +1 = toward open, -1 = toward closed, 0 = no move */
    int32_t steps;          /* half-steps to issue (>= 0) */
    int32_t target_steps;   /* absolute target position in steps */
    int32_t watchdog_steps; /* stall watchdog limit = ceil(steps * 1.25) */
} damper_move_plan_t;

/* Clamp an arbitrary integer to a valid percentage 0..100. */
uint8_t damper_clamp_pct(int32_t pct);

/* THE inversion — ZCL lift <-> internal open percentage. Inputs are
 * clamped to 0..100 first, so ZCL "invalid" 0xFF maps to lift 100
 * (= fully closed = open 0). Round-trips exactly for 0..100. */
uint8_t damper_open_to_lift(uint8_t open_pct);
uint8_t damper_lift_to_open(uint8_t lift_pct);

/* Percent <-> step conversions, rounded to nearest. range_steps <= 0 is
 * treated as uncalibrated: pct_to_steps returns 0, steps_to_pct returns 0. */
int32_t damper_pct_to_steps(uint8_t open_pct, int32_t range_steps);
uint8_t damper_steps_to_pct(int32_t steps, int32_t range_steps);

/* Current position as open percentage. */
uint8_t damper_open_pct(const damper_t *d);

/* Plan a move to target_open_pct. Returns false (and zeroes *plan) if the
 * damper is not calibrated/homed (position_valid false or range_steps <= 0);
 * the caller must home first. A zero-length move returns true with
 * direction 0 / steps 0. */
bool damper_plan_move(const damper_t *d, uint8_t target_open_pct,
                      damper_move_plan_t *plan);

/* Apply the outcome of a (possibly partial) move: advance position_steps by
 * steps_taken in direction, clamped into 0..range_steps. */
void damper_apply_steps(damper_t *d, int8_t direction, int32_t steps_taken);

#ifdef __cplusplus
}
#endif

#endif /* SMART_REGISTER_DAMPER_H */
