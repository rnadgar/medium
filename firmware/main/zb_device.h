/*
 * zb_device.h — Zigbee device model: endpoint/cluster creation, attribute
 * handlers, reporting and command dispatch (target-only; esp-zigbee-sdk).
 *
 * Device model (docs/architecture.md): sleepy end device, endpoint 1,
 * HA profile 0x0104, device id 0x0202 (Window Covering Device).
 * Manufacturer "OpenRegister", model "SR-4x10". Custom fail-safe cluster
 * 0xFC00 with manufacturer code 0x131B — the HA-side Zigbee2MQTT converter
 * and ZHA quirk address these attributes with manufacturer-specific ZCL
 * frames, so they are registered as manufacturer-specific here.
 */
#ifndef SMART_REGISTER_ZB_DEVICE_H
#define SMART_REGISTER_ZB_DEVICE_H

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZB_ENDPOINT               1
#define ZB_MANUFACTURER_CODE      0x131B
#define ZB_CLUSTER_ID_FAILSAFE    0xFC00

/* Fail-safe cluster attribute ids (docs/architecture.md table). */
#define ZB_ATTR_FS_TRIPPED        0x0000 /* bool,  R,  reportable */
#define ZB_ATTR_FS_THRESHOLD_PA   0x0001 /* u16,   RW, NVS, default 150 */
#define ZB_ATTR_FS_CLEAR_HYST_PA  0x0002 /* u16,   RW, NVS, default 30 */
#define ZB_ATTR_FS_LAST_TRIP_PA   0x0003 /* u16,   R,  default 0 */
#define ZB_ATTR_FS_AUTO_CLEAR     0x0004 /* bool,  RW, NVS, default true */
#define ZB_ATTR_FS_FAULT_CODE     0x0005 /* enum8, R,  reportable */

/* Create the Zigbee task (stack init, cluster/endpoint registration,
 * commissioning, main loop). Call after zb_sleep_init() and after the
 * drivers that command handlers rely on (stepper/motion, nvs). */
esp_err_t zb_device_start(void);

/* Motion-controller callbacks (position/fault/battery reporting) for
 * motion_start() — returns a `const motion_callbacks_t *` as void* to keep
 * this header free of stepper.h. */
const void *zb_device_motion_callbacks(void);

/* ---- Update paths called from other tasks (thread-safe: they take the
 *      esp_zb lock and no-op until the stack is running). ---------------- */

/* New pressure sample (signed Pa). If `report`, sends an attribute report
 * for ScaledValue (>= 10 Pa delta policy lives in pressure.c). */
void zb_device_update_pressure(int16_t pa, bool report);

/* Battery measurement -> Power Config attrs; reports both. */
void zb_device_update_battery(uint32_t pack_mv, uint8_t percent);

/* Damper position changed (internal open % — converted to ZCL lift here
 * via damper_open_to_lift, the single blessed call site). final=true also
 * sends the report. */
void zb_device_position_updated(uint8_t open_pct, bool final);

/* failsafe_tripped / fault_code changed: sync attrs from the failsafe
 * instance and report immediately with a fast-poll window
 * (docs/architecture.md reporting policy). */
void zb_device_failsafe_changed(bool tripped_changed, bool fault_changed);

#ifdef __cplusplus
}
#endif

#endif /* SMART_REGISTER_ZB_DEVICE_H */
