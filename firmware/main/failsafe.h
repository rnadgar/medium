/*
 * failsafe.h — over-pressure fail-open state machine.
 *
 * PURE LOGIC MODULE: no ESP-IDF includes, compiles on host (see
 * firmware/test/host). Inputs are (pressure_pa, now_ms); outputs are a
 * requested damper action + report flags. The caller (pressure.c on
 * target) wires the outputs to the motion controller and to the Zigbee
 * fail-safe cluster 0xFC00 — this module itself has zero Zigbee and zero
 * hardware dependencies, per docs/architecture.md.
 *
 * State machine (docs/architecture.md):
 *
 *   IDLE --blower pressure rises--> ARMED --p > threshold x2 samples--> TRIPPED
 *    ^                                |                                   |
 *    +------p ~ 0 sustained-----------+          p < threshold - hyst xM samples
 *                                                                        v
 *                                          RECOVERING --auto_clear?--> IDLE/ARMED
 *                                          (else latch in TRIPPED until HA clears)
 */
#ifndef SMART_REGISTER_FAILSAFE_H
#define SMART_REGISTER_FAILSAFE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* fault_code values — cluster 0xFC00 attr 0x0005 (enum8). */
typedef enum {
    FAILSAFE_FAULT_NONE = 0,
    FAILSAFE_FAULT_HOMING_TIMEOUT = 1,
    FAILSAFE_FAULT_STALL = 2,
    FAILSAFE_FAULT_SENSOR = 3,
} failsafe_fault_t;

typedef enum {
    FAILSAFE_STATE_IDLE = 0,   /* blower off */
    FAILSAFE_STATE_ARMED,      /* blower running, pressure below trip */
    FAILSAFE_STATE_TRIPPED,    /* fail-open active (incl. latched) */
    FAILSAFE_STATE_RECOVERING, /* transient: clear condition met this sample */
} failsafe_state_t;

/* Recommended adaptive sampling cadence (docs/architecture.md: 30 s base /
 * 120 s idle / 15 s active). */
typedef enum {
    FAILSAFE_SAMPLE_BASE = 0, /* 30 s  — blower state not yet established */
    FAILSAFE_SAMPLE_IDLE,     /* 120 s — blower confirmed off for a while */
    FAILSAFE_SAMPLE_ACTIVE,   /* 15 s  — blower running / tripped */
} failsafe_sample_class_t;

typedef struct {
    uint16_t threshold_pa;        /* attr 0x0001, default 150: trip above, 2 samples */
    uint16_t clear_hysteresis_pa; /* attr 0x0002, default 30: clear below thr-hyst */
    bool auto_clear_enable;       /* attr 0x0004, default true */
    uint8_t trip_confirm_samples; /* consecutive samples above threshold, default 2 */
    uint8_t clear_confirm_samples;/* consecutive samples below thr-hyst (M), default 3 */
    uint16_t arm_pressure_pa;     /* blower considered running at/above this, default 15 */
    uint16_t idle_pressure_pa;    /* "p ~ 0" at/below this, default 5 */
    uint32_t idle_disarm_ms;      /* p ~ 0 sustained this long -> IDLE, default 60000 */
} failsafe_config_t;

/* Actions requested by one input event. All flags are edge-triggered: they
 * describe what the caller must do NOW as a result of this event. */
typedef struct {
    bool request_open;    /* drive damper 100 % open immediately (no Zigbee) */
    bool request_resume;  /* restore last commanded position */
    bool tripped_changed; /* report attr 0x0000 (and 0x0003 on a new trip) */
    bool fault_changed;   /* report attr 0x0005 */
} failsafe_action_t;

typedef struct {
    failsafe_config_t cfg;
    failsafe_state_t state;

    /* Cluster-visible state */
    bool tripped;                  /* attr 0x0000 */
    uint16_t last_trip_pressure_pa;/* attr 0x0003 */
    uint8_t fault_code;            /* attr 0x0005 */

    /* Internals */
    uint8_t trip_count;
    uint8_t clear_count;
    bool low_since_valid;
    uint32_t low_since_ms;
    bool ever_armed;               /* distinguishes BASE from IDLE cadence */
    bool latched;                  /* tripped, recovered, auto_clear off */
} failsafe_t;

/* Defaults per the 0xFC00 attribute table in docs/architecture.md. */
failsafe_config_t failsafe_default_config(void);

/* Initialise; cfg == NULL uses defaults. */
void failsafe_init(failsafe_t *fs, const failsafe_config_t *cfg);

/* Feed one good pressure sample. now_ms is a monotonic millisecond clock
 * (wrap-safe over the u32 horizon as long as consecutive calls are < ~24 days
 * apart). Also clears a previously latched sensor fault (code 3). */
failsafe_action_t failsafe_sample(failsafe_t *fs, uint16_t pressure_pa,
                                  uint32_t now_ms);

/* Sensor unreadable (I2C failure): fault_code = 3 and fail open
 * (docs/architecture.md "Failure modes"). Idempotent. */
failsafe_action_t failsafe_sensor_fault(failsafe_t *fs, uint32_t now_ms);

/* Motor-side faults (homing timeout / stall) detected by the motion
 * controller; recorded here so cluster state stays in one place. */
failsafe_action_t failsafe_set_motor_fault(failsafe_t *fs, failsafe_fault_t code);

/* Operator/HA clear of a latched trip (auto_clear_enable == false), e.g. on
 * an explicit movement command from the coordinator. */
failsafe_action_t failsafe_manual_clear(failsafe_t *fs);

/* Runtime-writable attribute updates (writes to 0xFC00 from HA). */
void failsafe_set_threshold(failsafe_t *fs, uint16_t threshold_pa);
void failsafe_set_hysteresis(failsafe_t *fs, uint16_t hysteresis_pa);
void failsafe_set_auto_clear(failsafe_t *fs, bool enable);

/* Adaptive sampling recommendation for the pressure scheduler. */
failsafe_sample_class_t failsafe_sample_class(const failsafe_t *fs);

#ifdef __cplusplus
}
#endif

#endif /* SMART_REGISTER_FAILSAFE_H */
