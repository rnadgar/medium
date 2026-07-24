/*
 * battery.h — 4S LiFeS2 (Energizer L91-class) state-of-charge estimation.
 *
 * PURE LOGIC MODULE: no ESP-IDF includes, compiles on host. The ADC shim
 * (gated 1 M / 330 k divider on VBAT_SENSE) lives in battery_hw.c and is
 * target-only.
 *
 * LiFeS2 has a very flat open-circuit curve, so state of charge is derived
 * primarily from the pack voltage sampled UNDER MOTOR LOAD during a move
 * (docs/architecture.md "Battery measurement"); an unloaded daily reading is
 * mapped through a separate, shifted table.
 */
#ifndef SMART_REGISTER_BATTERY_H
#define SMART_REGISTER_BATTERY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pack voltage (mV) -> remaining percent (0..100), loaded curve
 * (~140 mA motor load on the pack). Clamped at both ends, piecewise
 * linear, monotonic non-decreasing in voltage. */
uint8_t battery_percent_from_loaded_mv(uint32_t pack_mv);

/* Same, for an unloaded/lightly loaded reading (daily housekeeping sample).
 * Coarser by nature — the loaded reading wins when both are available. */
uint8_t battery_percent_from_unloaded_mv(uint32_t pack_mv);

/* ZCL Power Configuration encodings:
 *  - BatteryVoltage 0x0020: uint8, 100 mV units.
 *  - BatteryPercentageRemaining 0x0021: uint8, 0.5 % units (0..200). */
uint8_t battery_zcl_voltage_from_mv(uint32_t pack_mv);
uint8_t battery_zcl_percentage_from_percent(uint8_t percent);

/* --- target-only ADC shim, implemented in battery_hw.c ------------------- */

/* Initialise ADC + divider-gate GPIO. */
int battery_hw_init(void);

/* Sample the pack through the gated divider. Returns pack millivolts, or 0
 * on failure. Enables VBAT_SENSE_EN around the burst read only (no standby
 * drain, per docs/power-budget.md). */
uint32_t battery_hw_read_pack_mv(void);

#ifdef __cplusplus
}
#endif

#endif /* SMART_REGISTER_BATTERY_H */
