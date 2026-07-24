/*
 * zb_sleep.h — light-sleep power management + poll-interval helpers
 * (target-only; ESP-IDF + esp-zigbee-sdk).
 *
 * Strategy (docs/architecture.md): automatic light sleep only —
 * CONFIG_PM_ENABLE + tickless idle + esp_zb_sleep_enable(true). Deep sleep
 * is deliberately not used (drops stack state, forces rejoin).
 */
#ifndef SMART_REGISTER_ZB_SLEEP_H
#define SMART_REGISTER_ZB_SLEEP_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configure esp_pm (light_sleep_enable) and enable Zigbee stack sleep.
 * MUST be called before esp_zb_init() — the stack samples the sleep flag
 * at init time (sleepy end device example pattern). */
esp_err_t zb_sleep_init(void);

/* Set the sleepy-end-device long poll interval (ms). Called after joining
 * or when the coordinator writes Poll Control attributes. */
void zb_sleep_set_long_poll_ms(uint32_t ms);

/* Open a fast-poll window for `ms` milliseconds: engaged after commands,
 * reports and joins so replies/acks flush through the parent promptly. */
void zb_sleep_fast_poll_window(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* SMART_REGISTER_ZB_SLEEP_H */
