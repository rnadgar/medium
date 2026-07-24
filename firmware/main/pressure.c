/*
 * pressure.c — SDP810 driver + adaptive sampling + fail-safe glue.
 * See pressure.h and docs/architecture.md ("Pressure sensing", "Sleep &
 * polling strategy", "Fail-safe state machine").
 */
#include "pressure.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board.h"
#include "nvs_state.h"
#include "stepper.h"
#include "zb_device.h"

static const char *TAG = "pressure";

/* ---- Sampling cadence (docs/architecture.md) ---------------------------- */
#define SAMPLE_BASE_MS    30000
#define SAMPLE_IDLE_MS    120000
#define SAMPLE_ACTIVE_MS  15000
#define REPORT_DELTA_PA   10

/* ---- SDP810-500Pa ------------------------------------------------------- */
#define SDP810_ADDR                0x25
#define SDP810_CMD_TRIG_DP_POLL_H  0x36 /* triggered, diff pressure, no
                                         * clock stretching: 0x362F */
#define SDP810_CMD_TRIG_DP_POLL_L  0x2F
#define SDP810_MEAS_DELAY_MS       50   /* triggered conversion ~45 ms */
#define SDP810_STARTUP_DELAY_MS    25   /* power-on to command-ready */
#define I2C_TIMEOUT_MS             50

/* Consecutive I2C failures before declaring fault_code 3. */
#define SENSOR_FAULT_AFTER_ERRORS  3

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_sample_sem;
static esp_timer_handle_t s_sched_timer;
static failsafe_t s_failsafe;
static int16_t s_last_pa;
static int16_t s_last_reported_pa = INT16_MIN;
static uint8_t s_error_streak;
static bool s_started;

failsafe_t *pressure_failsafe(void)
{
    return &s_failsafe;
}

int16_t pressure_last_pa(void)
{
    return s_last_pa;
}

/* Sensirion CRC-8: poly 0x31, init 0xFF, over 2 bytes. */
static uint8_t sdp_crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31)
                               : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

#if CONFIG_PRESSURE_SENSOR_XGZP /* reserved Kconfig hook, not yet wired */
/* ---- XGZP6897D cost-down fit option (STUB) ------------------------------
 * The XGZP needs a firmware auto-zero: its zero-offset drifts, so we keep a
 * long-term average of readings taken while the blower is confirmed off
 * (failsafe state IDLE) and subtract it. Driver TODO at Rev A bring-up. */
static int32_t s_xgzp_zero_offset_raw;

static void xgzp_auto_zero_update(int32_t raw, bool blower_off)
{
    if (blower_off) {
        /* IIR, tau ~ 64 samples. */
        s_xgzp_zero_offset_raw += (raw - s_xgzp_zero_offset_raw) / 64;
    }
}

static esp_err_t sensor_read_pa(int16_t *out_pa)
{
    (void)out_pa;
    return ESP_ERR_NOT_SUPPORTED; /* XGZP driver not implemented yet */
}
#else
/* ---- SDP810 triggered read ---------------------------------------------- */
static esp_err_t sensor_read_pa(int16_t *out_pa)
{
    const uint8_t cmd[2] = { SDP810_CMD_TRIG_DP_POLL_H, SDP810_CMD_TRIG_DP_POLL_L };
    esp_err_t err = i2c_master_transmit(s_dev, cmd, sizeof(cmd), I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(SDP810_MEAS_DELAY_MS));

    /* 9 bytes: dp[2]+crc, temp[2]+crc, scale[2]+crc. */
    uint8_t buf[9];
    err = i2c_master_receive(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    if (sdp_crc8(&buf[0], 2) != buf[2] || sdp_crc8(&buf[6], 2) != buf[8]) {
        return ESP_ERR_INVALID_CRC;
    }

    int16_t dp_raw = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t scale = (int16_t)((buf[6] << 8) | buf[7]); /* SDP810-500: 60/Pa */
    if (scale <= 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    /* Round to nearest Pa, sign-correct. */
    int32_t pa = (dp_raw >= 0) ? (dp_raw + scale / 2) / scale
                               : (dp_raw - scale / 2) / scale;
    *out_pa = (int16_t)pa;
    return ESP_OK;
}
#endif /* sensor variant */

/* ---- Fail-safe action plumbing ------------------------------------------ */

static void apply_failsafe_action(const failsafe_action_t *act)
{
    if (act->request_open) {
        /* Local fail-open: straight into the motion controller, zero Zigbee
         * involvement (docs/architecture.md fail-safe rationale). */
        motion_request_goto_open_pct(100, MOTION_ORIGIN_FAILSAFE);
    }
    if (act->request_resume) {
        motion_request_resume();
    }
    if (act->tripped_changed || act->fault_changed) {
        zb_device_failsafe_changed(act->tripped_changed, act->fault_changed);
    }
}

failsafe_action_t pressure_failsafe_manual_clear(void)
{
    failsafe_action_t act = failsafe_manual_clear(&s_failsafe);
    apply_failsafe_action(&act);
    return act;
}

void pressure_note_motor_fault(uint8_t fault_code)
{
    failsafe_action_t act =
        failsafe_set_motor_fault(&s_failsafe, (failsafe_fault_t)fault_code);
    /* Attribute update/report handled by zb_device via the same path. */
    if (act.fault_changed) {
        zb_device_failsafe_changed(false, true);
    }
}

/* ---- Adaptive scheduler -------------------------------------------------- */

static uint32_t next_interval_ms(void)
{
    switch (failsafe_sample_class(&s_failsafe)) {
    case FAILSAFE_SAMPLE_IDLE:
        return SAMPLE_IDLE_MS;
    case FAILSAFE_SAMPLE_ACTIVE:
        return SAMPLE_ACTIVE_MS;
    case FAILSAFE_SAMPLE_BASE:
    default:
        return SAMPLE_BASE_MS;
    }
}

static void sched_timer_cb(void *arg)
{
    (void)arg;
    xSemaphoreGive(s_sample_sem); /* wake the sampling task */
}

static void schedule_next(void)
{
    esp_timer_stop(s_sched_timer); /* ok if not running */
    ESP_ERROR_CHECK(esp_timer_start_once(s_sched_timer,
                                         (uint64_t)next_interval_ms() * 1000));
}

/* ---- Sampling task ------------------------------------------------------- */

static void do_sample(void)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    int16_t pa = 0;
    esp_err_t err;

    /* Power the sensor rail (shared load-switch refcount; 5 ms settle inside)
     * then give the sensor its own startup time before talking to it. */
    motion_rail_acquire();
    vTaskDelay(pdMS_TO_TICKS(SDP810_STARTUP_DELAY_MS));
    err = sensor_read_pa(&pa);
    motion_rail_release();

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sample failed: %s", esp_err_to_name(err));
        if (s_error_streak < 0xFF) {
            s_error_streak++;
        }
        if (s_error_streak >= SENSOR_FAULT_AFTER_ERRORS) {
            failsafe_action_t act = failsafe_sensor_fault(&s_failsafe, now_ms);
            apply_failsafe_action(&act);
        }
        return;
    }
    s_error_streak = 0;
    s_last_pa = pa;

    /* Fail-safe input is magnitude of duct static pressure; the module is
     * unsigned, negative (reverse-tap) readings clamp to 0. */
    uint16_t fs_pa = (pa > 0) ? (uint16_t)pa : 0;
    failsafe_action_t act = failsafe_sample(&s_failsafe, fs_pa, now_ms);
    apply_failsafe_action(&act);

    /* Report on >= 10 Pa delta (docs/architecture.md reporting policy). */
    bool report = (s_last_reported_pa == INT16_MIN) ||
                  (pa - s_last_reported_pa >= REPORT_DELTA_PA) ||
                  (s_last_reported_pa - pa >= REPORT_DELTA_PA);
    if (report) {
        s_last_reported_pa = pa;
    }
    zb_device_update_pressure(pa, report);
}

static void pressure_task(void *arg)
{
    (void)arg;
    for (;;) {
        do_sample();
        schedule_next();
        xSemaphoreTake(s_sample_sem, portMAX_DELAY);
    }
}

/* ---- Public API ---------------------------------------------------------- */

esp_err_t pressure_init(void)
{
    /* Fail-safe config from NVS-persisted cluster attributes. */
    failsafe_config_t cfg = failsafe_default_config();
    nvs_state_get_failsafe(&cfg.threshold_pa, &cfg.clear_hysteresis_pa,
                           &cfg.auto_clear_enable);
    failsafe_init(&s_failsafe, &cfg);

    /* I2C bus. The sensor is unpowered between samples; internal pull-ups
     * keep the bus lines defined while 3V3_SENS (and its stronger external
     * pull-ups) are gated off. */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1, /* auto-select */
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        return err;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SDP810_ADDR,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
}

esp_err_t pressure_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_sample_sem = xSemaphoreCreateBinary();
    if (s_sample_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const esp_timer_create_args_t targs = {
        .callback = sched_timer_cb,
        .name = "press_sched",
    };
    esp_err_t err = esp_timer_create(&targs, &s_sched_timer);
    if (err != ESP_OK) {
        return err;
    }
    BaseType_t ok = xTaskCreate(pressure_task, "pressure", 3072, NULL, 4, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}

void pressure_request_sample(void)
{
    if (s_sample_sem) {
        xSemaphoreGive(s_sample_sem);
    }
}
