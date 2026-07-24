/*
 * nvs_state.h — persisted device state (target-only).
 *
 * Keys (namespace "sregister"):
 *   open_pct     u8   last commanded/settled damper position (open %)
 *   range        i32  calibrated closed->open travel in half-steps
 *   move_pend    u8   dirty flag: set before a move, cleared after — if set
 *                     at boot, the step count is untrusted -> re-home
 *                     (docs/architecture.md "Position persistence")
 *   fs_thresh    u16  fail-safe cluster 0xFC00 attr 0x0001
 *   fs_hyst      u16  fail-safe cluster 0xFC00 attr 0x0002
 *   fs_autoclr   u8   fail-safe cluster 0xFC00 attr 0x0004
 */
#ifndef SMART_REGISTER_NVS_STATE_H
#define SMART_REGISTER_NVS_STATE_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise nvs_flash (erasing/re-initing on version mismatch) and open the
 * namespace. Must be called once, first thing in app_main. */
esp_err_t nvs_state_init(void);

uint8_t nvs_state_get_open_pct(uint8_t fallback);
void nvs_state_set_open_pct(uint8_t open_pct);

int32_t nvs_state_get_range_steps(int32_t fallback);
void nvs_state_set_range_steps(int32_t range_steps);

bool nvs_state_get_move_pending(void);
void nvs_state_set_move_pending(bool pending);

void nvs_state_get_failsafe(uint16_t *threshold_pa, uint16_t *hysteresis_pa,
                            bool *auto_clear);
void nvs_state_set_failsafe(uint16_t threshold_pa, uint16_t hysteresis_pa,
                            bool auto_clear);

#ifdef __cplusplus
}
#endif

#endif /* SMART_REGISTER_NVS_STATE_H */
