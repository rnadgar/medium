/*
 * stepper.h — hardware-facing stepper driver + motion controller
 * (target-only; ESP-IDF).
 *
 * Owns:
 *  - the DRV8833 half-step engine (GPTimer-paced, ISR-driven),
 *  - limit-switch reads (gated by LIMIT_SENSE_EN, 2-read debounce),
 *  - the switched-VBAT rail refcount (MOTOR_PWR_EN load switch — shared
 *    with the pressure sensor, see motion_rail_acquire/release),
 *  - homing / range calibration and the move_pending NVS protocol,
 *  - the motion task: executes goto/stop/home requests serially, keeps
 *    damper_t (pure logic, damper.h) as the position source of truth.
 *
 * Zigbee is deliberately NOT a dependency: the fail-safe path calls
 * motion_request_goto_open_pct(100, MOTION_ORIGIN_FAILSAFE) directly.
 * Reporting back to the network happens through the callbacks below,
 * registered by zb_device.c.
 */
#ifndef SMART_REGISTER_STEPPER_H
#define SMART_REGISTER_STEPPER_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MOTION_ORIGIN_NETWORK = 0, /* ZCL window covering command */
    MOTION_ORIGIN_LOCAL,       /* boot restore, button, etc. */
    MOTION_ORIGIN_FAILSAFE,    /* fail-open / resume from failsafe */
} motion_origin_t;

typedef struct {
    /* Position changed. final=true after a move settles (report + persist
     * trigger); final=false for coarse mid-move updates. */
    void (*on_position)(uint8_t open_pct, bool final);
    /* Motor-side fault (FAILSAFE_FAULT_HOMING_TIMEOUT / _STALL) or 0 when a
     * later fail-open succeeds against expectations. */
    void (*on_fault)(uint8_t fault_code);
    /* Pack millivolts sampled under motor load, once per move
     * (docs/architecture.md battery measurement). */
    void (*on_battery_loaded)(uint32_t pack_mv);
} motion_callbacks_t;

/* GPIO + GPTimer bring-up. Leaves everything unpowered/inactive. */
esp_err_t stepper_init(void);

/* Start the motion task. Loads position/range/move_pending from NVS; if the
 * move_pending dirty flag was set (reset mid-move), schedules an immediate
 * re-home per docs/architecture.md. cbs may be NULL (callbacks optional). */
esp_err_t motion_start(const motion_callbacks_t *cbs);

/* Queue a move to open_pct (0..100, clamped). MOTION_ORIGIN_FAILSAFE with
 * open_pct==100 is the fail-open path: it bypasses the fault lockout and
 * does not disturb the remembered commanded position. */
void motion_request_goto_open_pct(uint8_t open_pct, motion_origin_t origin);

/* Return to the last commanded position (failsafe auto-clear resume). */
void motion_request_resume(void);

/* Abort the in-flight move at the next half-step; position stays valid. */
void motion_request_stop(void);

/* Force a re-home + range calibration on the next move. */
void motion_request_rehome(void);

uint8_t motion_current_open_pct(void);
uint8_t motion_commanded_open_pct(void);
uint8_t motion_fault_code(void);

/* Switched-VBAT rail (TPS22919 / MOTOR_PWR_EN) refcount. First acquire
 * raises the enable and blocks ~5 ms for rail settle; last release drops it.
 * Used by pressure.c so sensor sampling and moves can overlap safely. */
void motion_rail_acquire(void);
void motion_rail_release(void);

#ifdef __cplusplus
}
#endif

#endif /* SMART_REGISTER_STEPPER_H */
