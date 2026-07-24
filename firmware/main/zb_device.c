/*
 * zb_device.c — Zigbee device model for the smart register.
 *
 * Structure follows the esp-zigbee-sdk HA examples (HA_on_off_light /
 * sleepy_end_device): a dedicated Zigbee task runs esp_zb_init + cluster
 * building + esp_zb_stack_main_loop(); esp_zb_app_signal_handler drives
 * commissioning and sleep; esp_zb_core_action_handler_register() dispatches
 * ZCL callbacks (window covering movement, attribute writes).
 *
 * API-UNCERTAINTY POLICY: everything that touches esp-zigbee-lib APIs whose
 * exact signature may drift between SDK 1.x minors is kept in this file (or
 * zb_sleep.c) behind small helpers, each tagged VERIFY AT FIRST TARGET BUILD.
 */
#include "zb_device.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "esp_zigbee_core.h"
/* Cluster/endpoint helper headers are pulled in by esp_zigbee_core.h in
 * SDK 1.x (esp_zigbee_cluster.h, esp_zigbee_endpoint.h, ha device defs). */

#include "battery.h"
#include "board.h"
#include "damper.h"
#include "failsafe.h"
#include "nvs_state.h"
#include "pressure.h"
#include "stepper.h"
#include "zb_sleep.h"

static const char *TAG = "zb_device";

/* ---- Identity ----------------------------------------------------------- */
/* ZCL character strings are length-prefixed. "OpenRegister" = 12 chars,
 * "SR-4x10" = 7 chars. The HA-side converter matches these exactly. */
static char s_mfr_name[] = "\x0c" "OpenRegister";
static char s_model_id[] = "\x07" "SR-4x10";
static char s_date_code[] = "\x08" "20260724";
static char s_sw_build[] = "\x05" "0.1.0";

#define ZB_DEVICE_VERSION 0
/* HA window covering device id 0x0202. The SDK defines
 * ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID; numeric fallback kept obvious. */
#define ZB_DEVICE_ID ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID

/* ZCL WindowCoveringType: 0x06 = Shutter (vane pack behaves like one). */
#define ZB_WINDOW_COVERING_TYPE 0x06
/* ZCL Window Covering ConfigStatus default: operational | online. */
#define ZB_WC_CONFIG_STATUS_DEFAULT 0x03
#define ZB_WC_MODE_DEFAULT 0x00

/* Poll Control server attribute ids (ZCL 3.16) — spelled numerically so the
 * generic cluster assembly below has no dependency on SDK enum names. */
#define ZB_ATTR_POLL_CHECKIN_INTERVAL  0x0000 /* u32, quarter-seconds */
#define ZB_ATTR_POLL_LONG_POLL         0x0001 /* u32, quarter-seconds */
#define ZB_ATTR_POLL_SHORT_POLL        0x0002 /* u16, quarter-seconds */
#define ZB_ATTR_POLL_FAST_POLL_TIMEOUT 0x0003 /* u16, quarter-seconds */

/* ---- Attribute shadow storage (referenced by the cluster attr lists) ---- */
static uint8_t s_attr_lift_pct;          /* WC 0x0008, ZCL lift % */
static uint8_t s_attr_batt_voltage;      /* PowerCfg 0x0020, 100 mV units */
static uint8_t s_attr_batt_pct;          /* PowerCfg 0x0021, 0.5 % units */
static int16_t s_attr_press_measured;    /* Pressure 0x0000, 0.1 kPa units */
static int16_t s_attr_press_scaled;      /* Pressure 0x0010, Pa (Scale=3) */
static int16_t s_attr_press_min_scaled = -500;
static int16_t s_attr_press_max_scaled = 500;
static int8_t s_attr_press_scale = 3;    /* Pa = ScaledValue * 10^(3-Scale) */
static uint8_t s_attr_fs_tripped;        /* bool */
static uint16_t s_attr_fs_threshold;
static uint16_t s_attr_fs_hyst;
static uint16_t s_attr_fs_last_trip;
static uint8_t s_attr_fs_auto_clear;     /* bool */
static uint8_t s_attr_fs_fault;          /* enum8 */

/* Poll Control server attrs (quarter-second units per ZCL). */
static uint32_t s_attr_checkin_interval = 0x3600; /* 57.6 min */
static uint32_t s_attr_long_poll_qs = (CONFIG_SMART_REGISTER_LONG_POLL_MS * 4) / 1000;
static uint16_t s_attr_short_poll_qs = 2;         /* 0.5 s */
static uint16_t s_attr_fast_poll_timeout = 0x28;  /* 10 s */

static volatile bool s_stack_running;

/* ---- Identify: status LED blink ----------------------------------------- */

static esp_timer_handle_t s_identify_timer;
static volatile bool s_identifying;

static void identify_blink_cb(void *arg)
{
    (void)arg;
    static bool on;
    on = s_identifying ? !on : false;
    gpio_set_level(PIN_STATUS_LED, on);
    if (!s_identifying) {
        esp_timer_stop(s_identify_timer);
    }
}

static void identify_notify_cb(uint8_t identify_on)
{
    s_identifying = (identify_on != 0);
    if (identify_on) {
        if (!esp_timer_is_active(s_identify_timer)) {
            esp_timer_start_periodic(s_identify_timer, 250 * 1000);
        }
    }
    /* Timer callback turns itself (and the LED) off when done. */
}

/* ---- Small helpers ------------------------------------------------------- */

static void zb_set_attr(uint16_t cluster_id, uint16_t attr_id, void *value)
{
    esp_zb_zcl_set_attribute_val(ZB_ENDPOINT, cluster_id,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attr_id,
                                 value, false);
}

/* VERIFY AT FIRST TARGET BUILD: manufacturer-specific local attribute set.
 * SDK 1.x provides esp_zb_zcl_set_manufacturer_attribute_val() for attrs
 * registered with a manufacturer code. If the deployed version stores custom
 * cluster attrs as plain attributes instead, fall back to zb_set_attr(). */
static void zb_set_fc00_attr(uint16_t attr_id, void *value)
{
    esp_zb_zcl_set_manufacturer_attribute_val(ZB_ENDPOINT,
                                              ZB_CLUSTER_ID_FAILSAFE,
                                              ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                              ZB_MANUFACTURER_CODE, attr_id,
                                              value, false);
}

/* Send an attribute report to the bound destination (coordinator). With
 * ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT the stack resolves the
 * destination from the binding table, so dst fields stay zero. */
static void zb_report_attr(uint16_t cluster_id, uint16_t attr_id, bool mfr)
{
    esp_zb_zcl_report_attr_cmd_t cmd = { 0 };
    cmd.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
    cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
    cmd.clusterID = cluster_id;
    cmd.attributeID = attr_id;
    cmd.zcl_basic_cmd.src_endpoint = ZB_ENDPOINT;
    if (mfr) {
        /* VERIFY AT FIRST TARGET BUILD: manufacturer-specific report frame
         * fields (added to esp_zb_zcl_report_attr_cmd_t during SDK 1.x; the
         * HA-side converter expects mfr-specific frames for 0xFC00). If the
         * deployed SDK lacks these fields, drop them — the converter also
         * parses standard report frames as a fallback. */
        cmd.manuf_specific = 1;
        cmd.manuf_code = ZB_MANUFACTURER_CODE;
    }
    esp_zb_zcl_report_attr_cmd_req(&cmd);
}

/* ---- Public update paths (called from other tasks) ----------------------- */

void zb_device_update_pressure(int16_t pa, bool report)
{
    if (!s_stack_running) {
        return;
    }
    int16_t scaled = pa; /* Scale = 3 -> ScaledValue is whole Pa */
    /* MeasuredValue kept coherent: 0.1 kPa units = Pa / 100, rounded. */
    int16_t measured = (int16_t)((pa >= 0) ? (pa + 50) / 100 : (pa - 50) / 100);

    esp_zb_lock_acquire(portMAX_DELAY);
    s_attr_press_scaled = scaled;
    s_attr_press_measured = measured;
    zb_set_attr(ESP_ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT,
                ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_SCALED_VALUE_ID, &scaled);
    zb_set_attr(ESP_ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT,
                ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_VALUE_ID, &measured);
    if (report) {
        zb_report_attr(ESP_ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT,
                       ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_SCALED_VALUE_ID,
                       false);
        zb_sleep_fast_poll_window(CONFIG_SMART_REGISTER_FAST_POLL_WINDOW_MS);
    }
    esp_zb_lock_release();
}

void zb_device_update_battery(uint32_t pack_mv, uint8_t percent)
{
    if (!s_stack_running) {
        return;
    }
    uint8_t volt = (uint8_t)((pack_mv + 50) / 100);
    uint8_t pct2 = (uint8_t)((percent > 100 ? 100 : percent) * 2);

    esp_zb_lock_acquire(portMAX_DELAY);
    s_attr_batt_voltage = volt;
    s_attr_batt_pct = pct2;
    zb_set_attr(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID, &volt);
    zb_set_attr(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
                &pct2);
    zb_report_attr(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                   ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
                   false);
    esp_zb_lock_release();
}

void zb_device_position_updated(uint8_t open_pct, bool final)
{
    if (!s_stack_running) {
        return;
    }
    /* THE inversion — via damper.c only. */
    uint8_t lift = damper_open_to_lift(open_pct);

    esp_zb_lock_acquire(portMAX_DELAY);
    s_attr_lift_pct = lift;
    zb_set_attr(ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
                ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
                &lift);
    if (final) {
        zb_report_attr(ESP_ZB_ZCL_CLUSTER_ID_WINDOW_COVERING,
                       ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
                       false);
        zb_sleep_fast_poll_window(CONFIG_SMART_REGISTER_FAST_POLL_WINDOW_MS);
    }
    esp_zb_lock_release();
}

void zb_device_failsafe_changed(bool tripped_changed, bool fault_changed)
{
    if (!s_stack_running) {
        return;
    }
    failsafe_t *fs = pressure_failsafe();
    uint8_t tripped = fs->tripped ? 1 : 0;
    uint16_t last_trip = fs->last_trip_pressure_pa;
    uint8_t fault = fs->fault_code;

    esp_zb_lock_acquire(portMAX_DELAY);
    s_attr_fs_tripped = tripped;
    s_attr_fs_last_trip = last_trip;
    s_attr_fs_fault = fault;
    zb_set_fc00_attr(ZB_ATTR_FS_TRIPPED, &tripped);
    zb_set_fc00_attr(ZB_ATTR_FS_LAST_TRIP_PA, &last_trip);
    zb_set_fc00_attr(ZB_ATTR_FS_FAULT_CODE, &fault);
    /* Report immediately with a fast-poll window so the report flushes
     * through the parent promptly (docs/architecture.md). */
    if (tripped_changed) {
        zb_report_attr(ZB_CLUSTER_ID_FAILSAFE, ZB_ATTR_FS_TRIPPED, true);
    }
    if (fault_changed) {
        zb_report_attr(ZB_CLUSTER_ID_FAILSAFE, ZB_ATTR_FS_FAULT_CODE, true);
    }
    zb_sleep_fast_poll_window(CONFIG_SMART_REGISTER_FAST_POLL_WINDOW_MS);
    esp_zb_lock_release();
}

/* ---- Motion callbacks (registered with motion_start in main.c) ---------- */

static void motion_on_position(uint8_t open_pct, bool final)
{
    zb_device_position_updated(open_pct, final);
}

static void motion_on_fault(uint8_t fault_code)
{
    /* Record in the failsafe state (single owner of fault_code) and let the
     * shared path update + report the attribute. */
    pressure_note_motor_fault(fault_code);
}

static void motion_on_battery_loaded(uint32_t pack_mv)
{
    zb_device_update_battery(pack_mv, battery_percent_from_loaded_mv(pack_mv));
}

const void *zb_device_motion_callbacks(void); /* forward (used by main.c) */

const void *zb_device_motion_callbacks(void)
{
    static const motion_callbacks_t cbs = {
        .on_position = motion_on_position,
        .on_fault = motion_on_fault,
        .on_battery_loaded = motion_on_battery_loaded,
    };
    return &cbs;
}

/* ---- ZCL command / attribute dispatch ------------------------------------ */

static esp_err_t handle_window_covering(
    const esp_zb_zcl_window_covering_movement_message_t *msg)
{
    ESP_RETURN_ON_FALSE(msg, ESP_FAIL, TAG, "empty movement message");
    ESP_RETURN_ON_FALSE(msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS,
                        ESP_ERR_INVALID_ARG, TAG, "movement msg status %d",
                        msg->info.status);

    /* An explicit movement command from HA releases a latched fail-open
     * (auto_clear_enable == false -> "latch until cleared from HA"). The
     * resume implied by the clear is superseded by this new command, so the
     * latch is cleared without applying request_resume. */
    failsafe_t *fs = pressure_failsafe();
    failsafe_action_t clr = failsafe_manual_clear(fs);
    if (clr.tripped_changed) {
        zb_device_failsafe_changed(true, false);
    }

    switch (msg->command) {
    case ESP_ZB_ZCL_CMD_WINDOW_COVERING_UP_OPEN:
        ESP_LOGI(TAG, "cmd UpOrOpen");
        motion_request_goto_open_pct(100, MOTION_ORIGIN_NETWORK);
        break;
    case ESP_ZB_ZCL_CMD_WINDOW_COVERING_DOWN_CLOSE:
        ESP_LOGI(TAG, "cmd DownOrClose");
        motion_request_goto_open_pct(0, MOTION_ORIGIN_NETWORK);
        break;
    case ESP_ZB_ZCL_CMD_WINDOW_COVERING_STOP:
        ESP_LOGI(TAG, "cmd Stop");
        motion_request_stop();
        break;
    case ESP_ZB_ZCL_CMD_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE: {
        /* VERIFY AT FIRST TARGET BUILD: payload union member name for the
         * lift percentage (percentage_lift_value in SDK 1.x). */
        uint8_t lift = msg->payload.percentage_lift_value;
        uint8_t open_pct = damper_lift_to_open(lift); /* THE inversion */
        ESP_LOGI(TAG, "cmd GoToLiftPercentage lift=%u -> open=%u", lift,
                 open_pct);
        motion_request_goto_open_pct(open_pct, MOTION_ORIGIN_NETWORK);
        break;
    }
    default:
        ESP_LOGW(TAG, "unhandled window covering cmd %d", msg->command);
        return ESP_ERR_NOT_SUPPORTED;
    }
    zb_sleep_fast_poll_window(CONFIG_SMART_REGISTER_FAST_POLL_WINDOW_MS);
    return ESP_OK;
}

static esp_err_t handle_set_attr_value(
    const esp_zb_zcl_set_attr_value_message_t *msg)
{
    ESP_RETURN_ON_FALSE(msg, ESP_FAIL, TAG, "empty set-attr message");
    ESP_RETURN_ON_FALSE(msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS,
                        ESP_ERR_INVALID_ARG, TAG, "set-attr status %d",
                        msg->info.status);

    if (msg->info.dst_endpoint != ZB_ENDPOINT) {
        return ESP_OK;
    }

    if (msg->info.cluster == ZB_CLUSTER_ID_FAILSAFE) {
        failsafe_t *fs = pressure_failsafe();
        switch (msg->attribute.id) {
        case ZB_ATTR_FS_THRESHOLD_PA:
            s_attr_fs_threshold = *(uint16_t *)msg->attribute.data.value;
            failsafe_set_threshold(fs, s_attr_fs_threshold);
            break;
        case ZB_ATTR_FS_CLEAR_HYST_PA:
            s_attr_fs_hyst = *(uint16_t *)msg->attribute.data.value;
            failsafe_set_hysteresis(fs, s_attr_fs_hyst);
            break;
        case ZB_ATTR_FS_AUTO_CLEAR:
            s_attr_fs_auto_clear = *(uint8_t *)msg->attribute.data.value;
            failsafe_set_auto_clear(fs, s_attr_fs_auto_clear != 0);
            break;
        default:
            return ESP_OK; /* read-only attrs rejected by the stack */
        }
        /* Writable 0xFC00 attrs are persisted (docs table: "RW, NVS"). */
        nvs_state_set_failsafe(fs->cfg.threshold_pa,
                               fs->cfg.clear_hysteresis_pa,
                               fs->cfg.auto_clear_enable);
        ESP_LOGI(TAG, "failsafe cfg: thr=%u hyst=%u auto=%u",
                 fs->cfg.threshold_pa, fs->cfg.clear_hysteresis_pa,
                 (unsigned)fs->cfg.auto_clear_enable);
        return ESP_OK;
    }

    if (msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_POLL_CONTROL &&
        msg->attribute.id == ZB_ATTR_POLL_LONG_POLL) {
        uint32_t qs = *(uint32_t *)msg->attribute.data.value;
        s_attr_long_poll_qs = qs;
        zb_sleep_set_long_poll_ms(qs * 250);
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id,
                                   const void *message)
{
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        return handle_set_attr_value(
            (const esp_zb_zcl_set_attr_value_message_t *)message);
    case ESP_ZB_CORE_WINDOW_COVERING_MOVEMENT_CB_ID:
        return handle_window_covering(
            (const esp_zb_zcl_window_covering_movement_message_t *)message);
    default:
        ESP_LOGD(TAG, "unhandled zb action 0x%x", callback_id);
        return ESP_OK;
    }
}

/* ---- Deferred driver init (sleepy end device example pattern) ------------ */

static esp_err_t deferred_driver_init(void)
{
    static bool done;
    if (done) {
        return ESP_OK;
    }
    /* Pressure sampling only starts once the stack is up so the first
     * samples/report cannot race commissioning. */
    ESP_RETURN_ON_ERROR(pressure_start(), TAG, "pressure_start");
    done = true;
    return ESP_OK;
}

/* ---- Commissioning / stack signals --------------------------------------- */

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "stack initialized");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "device started (%s factory new)",
                     esp_zb_bdb_is_factory_new() ? "" : "not");
            s_stack_running = true;
            ESP_ERROR_CHECK(deferred_driver_init());
            if (esp_zb_bdb_is_factory_new()) {
                esp_zb_bdb_start_top_level_commissioning(
                    ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                zb_sleep_set_long_poll_ms(CONFIG_SMART_REGISTER_LONG_POLL_MS);
                zb_sleep_fast_poll_window(
                    CONFIG_SMART_REGISTER_FAST_POLL_WINDOW_MS);
            }
        } else {
            ESP_LOGW(TAG, "stack %s failure (%s), retrying",
                     esp_zb_zdo_signal_to_string(sig_type),
                     esp_err_to_name(err_status));
            esp_zb_scheduler_alarm(bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "joined, PAN 0x%04hx ch %d",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel());
            zb_sleep_set_long_poll_ms(CONFIG_SMART_REGISTER_LONG_POLL_MS);
            zb_sleep_fast_poll_window(CONFIG_SMART_REGISTER_FAST_POLL_WINDOW_MS);
            /* Push current state so HA has fresh values right after join. */
            zb_device_position_updated(motion_current_open_pct(), true);
        } else {
            ESP_LOGI(TAG, "steering failed (%s), retry in 1 s",
                     esp_err_to_name(err_status));
            esp_zb_scheduler_alarm(bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        ESP_LOGW(TAG, "left network");
        break;

    case ESP_ZB_COMMON_SIGNAL_CAN_SLEEP:
        /* Automatic light sleep between MAC polls. */
        esp_zb_sleep_now();
        break;

    default:
        ESP_LOGD(TAG, "ZDO signal %s (0x%x), status %s",
                 esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

/* ---- Cluster building ----------------------------------------------------
 * NOTE ON ATTRIBUTE ACCESS FLAGS: the HA layer sends configure-reporting for
 * lift %, battery %, pressure ScaledValue, failsafe_tripped and fault_code —
 * all of those attrs must carry ESP_ZB_ZCL_ATTR_ACCESS_REPORTING. Standard
 * cluster attrs (lift/battery/measured value) are reportable per ZCL already;
 * the custom 0xFC00 attrs get the flag explicitly below. */

/* VERIFY AT FIRST TARGET BUILD: manufacturer-specific attribute registration.
 * SDK 1.x: esp_zb_custom_cluster_add_manufacturer_attr(list, attr_id,
 * manuf_code, type, access, value). If the deployed minor only offers
 * esp_zb_custom_cluster_add_custom_attr() (no manuf code), the HA quirk's
 * mfr-specific reads will fail — fix HERE, not at the call sites. */
static void fc00_add_attr(esp_zb_attribute_list_t *list, uint16_t attr_id,
                          uint8_t type, uint8_t access, void *value)
{
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_manufacturer_attr(
        list, attr_id, ZB_MANUFACTURER_CODE, type, access, value));
}

static esp_zb_cluster_list_t *build_clusters(void)
{
    esp_zb_cluster_list_t *cl = esp_zb_zcl_cluster_list_create();

    /* Basic */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = 0x03, /* battery */
    };
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(&basic_cfg);
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(
        basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, s_mfr_name));
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(
        basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, s_model_id));
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(
        basic, ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, s_date_code));
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(
        basic, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, s_sw_build));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(
        cl, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    /* Identify */
    esp_zb_identify_cluster_cfg_t ident_cfg = {
        .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
    };
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(
        cl, esp_zb_identify_cluster_create(&ident_cfg),
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    /* Power Configuration: battery voltage + percentage (reportable). */
    esp_zb_attribute_list_t *power = esp_zb_power_config_cluster_create(NULL);
    ESP_ERROR_CHECK(esp_zb_power_config_cluster_add_attr(
        power, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
        &s_attr_batt_voltage));
    ESP_ERROR_CHECK(esp_zb_power_config_cluster_add_attr(
        power, ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
        &s_attr_batt_pct));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_power_config_cluster(
        cl, power, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    /* Poll Control server. VERIFY AT FIRST TARGET BUILD: SDK 1.6 has no
     * dedicated poll-control create helper in all minors — the cluster is
     * assembled generically here. Units are quarter-seconds per ZCL. */
    esp_zb_attribute_list_t *poll =
        esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_POLL_CONTROL);
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(
        poll, ZB_ATTR_POLL_CHECKIN_INTERVAL, ESP_ZB_ZCL_ATTR_TYPE_U32,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_attr_checkin_interval));
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(
        poll, ZB_ATTR_POLL_LONG_POLL, ESP_ZB_ZCL_ATTR_TYPE_U32,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_attr_long_poll_qs));
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(
        poll, ZB_ATTR_POLL_SHORT_POLL, ESP_ZB_ZCL_ATTR_TYPE_U16,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_attr_short_poll_qs));
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(
        poll, ZB_ATTR_POLL_FAST_POLL_TIMEOUT, ESP_ZB_ZCL_ATTR_TYPE_U16,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_attr_fast_poll_timeout));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(
        cl, poll, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    /* Window Covering — primary control. */
    esp_zb_window_covering_cluster_cfg_t wc_cfg = {
        .covering_type = ZB_WINDOW_COVERING_TYPE,
        .covering_status = ZB_WC_CONFIG_STATUS_DEFAULT,
        .covering_mode = ZB_WC_MODE_DEFAULT,
    };
    esp_zb_attribute_list_t *wc = esp_zb_window_covering_cluster_create(&wc_cfg);
    s_attr_lift_pct = damper_open_to_lift(motion_current_open_pct());
    ESP_ERROR_CHECK(esp_zb_window_covering_cluster_add_attr(
        wc, ESP_ZB_ZCL_ATTR_WINDOW_COVERING_CURRENT_POSITION_LIFT_PERCENTAGE_ID,
        &s_attr_lift_pct));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_window_covering_cluster(
        cl, wc, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    /* Pressure Measurement: MeasuredValue (0.1 kPa) kept coherent with
     * ScaledValue/Scale. Scale = 3 -> ScaledValue in whole Pa; the HA side
     * computes Pa = ScaledValue * 10^(3 - Scale). */
    esp_zb_pressure_meas_cluster_cfg_t press_cfg = {
        .measured_value = 0,
        .min_value = -5, /* -500 Pa in 0.1 kPa units */
        .max_value = 5,
    };
    esp_zb_attribute_list_t *press =
        esp_zb_pressure_meas_cluster_create(&press_cfg);
    ESP_ERROR_CHECK(esp_zb_pressure_meas_cluster_add_attr(
        press, ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_SCALED_VALUE_ID,
        &s_attr_press_scaled));
    ESP_ERROR_CHECK(esp_zb_pressure_meas_cluster_add_attr(
        press, ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_MIN_SCALED_VALUE_ID,
        &s_attr_press_min_scaled));
    ESP_ERROR_CHECK(esp_zb_pressure_meas_cluster_add_attr(
        press, ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_MAX_SCALED_VALUE_ID,
        &s_attr_press_max_scaled));
    ESP_ERROR_CHECK(esp_zb_pressure_meas_cluster_add_attr(
        press, ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_SCALE_ID,
        &s_attr_press_scale));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_pressure_meas_cluster(
        cl, press, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    /* Fail-safe cluster 0xFC00, manufacturer code 0x131B. All attrs
     * manufacturer-specific (the HA converter/quirk uses mfr-specific ZCL
     * frames). tripped + fault_code reportable; threshold/hyst/auto_clear
     * RW + persisted. Defaults restored from NVS before this runs. */
    {
        failsafe_t *fs = pressure_failsafe();
        s_attr_fs_threshold = fs->cfg.threshold_pa;
        s_attr_fs_hyst = fs->cfg.clear_hysteresis_pa;
        s_attr_fs_auto_clear = fs->cfg.auto_clear_enable ? 1 : 0;
        s_attr_fs_tripped = fs->tripped ? 1 : 0;
        s_attr_fs_last_trip = fs->last_trip_pressure_pa;
        s_attr_fs_fault = fs->fault_code;
    }
    esp_zb_attribute_list_t *fc00 =
        esp_zb_zcl_attr_list_create(ZB_CLUSTER_ID_FAILSAFE);
    fc00_add_attr(fc00, ZB_ATTR_FS_TRIPPED, ESP_ZB_ZCL_ATTR_TYPE_BOOL,
                  ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY |
                      ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                  &s_attr_fs_tripped);
    fc00_add_attr(fc00, ZB_ATTR_FS_THRESHOLD_PA, ESP_ZB_ZCL_ATTR_TYPE_U16,
                  ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_attr_fs_threshold);
    fc00_add_attr(fc00, ZB_ATTR_FS_CLEAR_HYST_PA, ESP_ZB_ZCL_ATTR_TYPE_U16,
                  ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_attr_fs_hyst);
    fc00_add_attr(fc00, ZB_ATTR_FS_LAST_TRIP_PA, ESP_ZB_ZCL_ATTR_TYPE_U16,
                  ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &s_attr_fs_last_trip);
    fc00_add_attr(fc00, ZB_ATTR_FS_AUTO_CLEAR, ESP_ZB_ZCL_ATTR_TYPE_BOOL,
                  ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &s_attr_fs_auto_clear);
    fc00_add_attr(fc00, ZB_ATTR_FS_FAULT_CODE, ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM,
                  ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY |
                      ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                  &s_attr_fs_fault);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(
        cl, fc00, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    /* OTA Upgrade client is phase 2 — partition slots are reserved in
     * partitions.csv; the cluster is intentionally not created yet. */

    return cl;
}

/* ---- Zigbee task ---------------------------------------------------------- */

static void zb_task(void *arg)
{
    (void)arg;

    /* Sleepy End Device (rx_on_when_idle = false). Keep-alive matches the
     * ~5 s long poll; aging timeout 64 min. */
    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
            .keep_alive = 3000,
        },
    };
    esp_zb_init(&zb_cfg);
    esp_zb_set_rx_on_when_idle(false);

    esp_zb_cluster_list_t *clusters = build_clusters();
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = ZB_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ZB_DEVICE_ID, /* 0x0202 window covering device */
        .app_device_version = ZB_DEVICE_VERSION,
    };
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(ep_list, clusters, ep_cfg));
    ESP_ERROR_CHECK(esp_zb_device_register(ep_list));

    /* VERIFY AT FIRST TARGET BUILD: node-descriptor manufacturer code setter
     * name (esp_zb_set_node_descriptor_manufacturer_code in SDK 1.x). */
    esp_zb_set_node_descriptor_manufacturer_code(ZB_MANUFACTURER_CODE);

    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_identify_notify_handler_register(ZB_ENDPOINT, identify_notify_cb);
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop(); /* never returns; services CAN_SLEEP signals */
}

esp_err_t zb_device_start(void)
{
    /* Status LED + identify blink timer. */
    gpio_config_t led = {
        .pin_bit_mask = 1ULL << PIN_STATUS_LED,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led));
    gpio_set_level(PIN_STATUS_LED, 0);
    const esp_timer_create_args_t targs = {
        .callback = identify_blink_cb,
        .name = "identify",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_identify_timer));

    BaseType_t ok = xTaskCreate(zb_task, "zigbee", 6144, NULL, 6, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
