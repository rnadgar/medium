/*
 * pressure.h — differential pressure sensing + fail-safe evaluation
 * (target-only; ESP-IDF).
 *
 * Owns the SDP810 I2C driver (triggered-measurement mode), the adaptive
 * sampling scheduler (30 s base / 120 s idle / 15 s active, driven by
 * failsafe_sample_class()), power gating of the sensor rail via the shared
 * MOTOR_PWR_EN switch (motion_rail_acquire/release), and the failsafe_t
 * instance. On failsafe actions it calls the motion controller directly
 * (fail-open has zero Zigbee dependency) and pushes attribute/report
 * updates through zb_device.c.
 *
 * XGZP6897D (cost-down fit option) support is stubbed behind
 * PRESSURE_SENSOR_XGZP with an auto-zero hook.
 */
#ifndef SMART_REGISTER_PRESSURE_H
#define SMART_REGISTER_PRESSURE_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#include "failsafe.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-time bus/GPIO init. Cheap; safe before the Zigbee stack is up. */
esp_err_t pressure_init(void);

/* Start the sampling task + adaptive timer. Called from the deferred driver
 * init in zb_device.c once the stack is running (sleepy-end-device example
 * pattern) so early samples cannot race commissioning. */
esp_err_t pressure_start(void);

/* Trigger an immediate out-of-schedule sample (e.g. diagnostics). */
void pressure_request_sample(void);

/* Runtime access to the failsafe instance for ZCL attribute writes/reads.
 * Serialised: call only from the Zigbee task via zb_device.c. */
failsafe_t *pressure_failsafe(void);

/* Propagate an HA-side manual clear / movement command to the latched
 * fail-safe (returns the resulting action already applied to the motion
 * controller; caller reports flags). */
failsafe_action_t pressure_failsafe_manual_clear(void);

/* Record a motor fault (homing timeout/stall) into the failsafe state. */
void pressure_note_motor_fault(uint8_t fault_code);

/* Last good signed pressure sample in Pa (0 if none yet). */
int16_t pressure_last_pa(void);

#ifdef __cplusplus
}
#endif

#endif /* SMART_REGISTER_PRESSURE_H */
