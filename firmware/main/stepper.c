/*
 * stepper.c — DRV8833 half-step engine + motion controller (target-only).
 * See stepper.h. Spec: docs/architecture.md "Damper control".
 *
 * Move sequence (power-budget design rule): MOTOR_PWR_EN high -> 5 ms rail
 * settle -> LIMIT_SENSE_EN high -> step -> coils off -> both enables low.
 */
#include "stepper.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "board.h"
#include "battery.h"
#include "damper.h"
#include "failsafe.h" /* pure header — fault code values only */
#include "nvs_state.h"

static const char *TAG = "stepper";

/* ---- Mechanism constants ------------------------------------------------ */
/* 28BYJ-48: ~4096 half-steps/output-rev; ~3:1 pinion->sector; ~90 deg vane
 * travel => ~3072 half-steps closed->open. Used only as the pre-calibration
 * estimate; the real range comes from limit-switch calibration (NVS). */
#define DEFAULT_RANGE_STEPS   3072
#define MOVE_STEP_RATE_HZ     600  /* docs: 500-800 half-steps/s */
#define HOMING_STEP_RATE_HZ   300  /* homing at reduced rate */
#define HOMING_BACKOFF_STEPS  48   /* back off the switch after homing */
#define RAIL_SETTLE_MS        5
#define LIMIT_DEBOUNCE_READS  2    /* consecutive active reads */
#define BATTERY_SAMPLE_DELAY_MS 300 /* into the move, under full load */

/* Half-step drive table for a bipolar motor on DRV8833: one row per
 * half-step, columns AIN1, AIN2, BIN1, BIN2. Increasing index = toward
 * OPEN (swap here if the Rev A wiring turns out mirrored). */
static const uint8_t k_half_step[8][4] = {
    { 1, 0, 0, 0 }, /* A+       */
    { 1, 0, 1, 0 }, /* A+ B+    */
    { 0, 0, 1, 0 }, /*    B+    */
    { 0, 1, 1, 0 }, /* A- B+    */
    { 0, 1, 0, 0 }, /* A-       */
    { 0, 1, 0, 1 }, /* A- B-    */
    { 0, 0, 0, 1 }, /*    B-    */
    { 1, 0, 0, 1 }, /* A+ B-    */
};

/* ---- Step-engine (ISR) state ------------------------------------------- */

typedef enum {
    STEP_RES_DONE = 0,     /* target step count reached */
    STEP_RES_LIMIT_OPEN,   /* stopped on OPEN limit (debounced) */
    STEP_RES_LIMIT_CLOSED, /* stopped on CLOSED limit (debounced) */
    STEP_RES_WATCHDOG,     /* max_steps exceeded — stall/homing timeout */
    STEP_RES_STOPPED,      /* aborted by motion_request_stop() */
} step_result_t;

typedef struct {
    int8_t direction;          /* +1 toward open, -1 toward closed */
    int32_t target_steps;      /* stop after this many steps */
    int32_t max_steps;         /* hard watchdog (>= target_steps) */
    bool stop_on_open_limit;
    bool stop_on_closed_limit;
} step_run_cfg_t;

static struct {
    step_run_cfg_t cfg;
    volatile int32_t steps_taken;
    volatile step_result_t result;
    volatile bool active;
    uint8_t phase;             /* index into k_half_step */
    uint8_t open_hits;         /* debounce counters */
    uint8_t closed_hits;
} s_run;

static gptimer_handle_t s_timer;
static SemaphoreHandle_t s_run_done;
static volatile bool s_stop_req;

/* ---- Switched-rail refcount -------------------------------------------- */

static SemaphoreHandle_t s_rail_mutex;
static int s_rail_refs;

void motion_rail_acquire(void)
{
    xSemaphoreTake(s_rail_mutex, portMAX_DELAY);
    if (s_rail_refs++ == 0) {
        gpio_set_level(PIN_MOTOR_PWR_EN, 1);
        vTaskDelay(pdMS_TO_TICKS(RAIL_SETTLE_MS)); /* rail settle */
    }
    xSemaphoreGive(s_rail_mutex);
}

void motion_rail_release(void)
{
    xSemaphoreTake(s_rail_mutex, portMAX_DELAY);
    if (s_rail_refs > 0 && --s_rail_refs == 0) {
        gpio_set_level(PIN_MOTOR_PWR_EN, 0);
    }
    xSemaphoreGive(s_rail_mutex);
}

/* ---- Coil / limit helpers ---------------------------------------------- */

static void coils_write(const uint8_t row[4])
{
    gpio_set_level(PIN_MOTOR_AIN1, row[0]);
    gpio_set_level(PIN_MOTOR_AIN2, row[1]);
    gpio_set_level(PIN_MOTOR_BIN1, row[2]);
    gpio_set_level(PIN_MOTOR_BIN2, row[3]);
}

static void coils_off(void)
{
    static const uint8_t off[4] = { 0, 0, 0, 0 };
    coils_write(off);
}

/* ---- GPTimer ISR: one half-step per alarm ------------------------------ */

static bool IRAM_ATTR step_timer_cb(gptimer_handle_t timer,
                                    const gptimer_alarm_event_data_t *edata,
                                    void *user_ctx)
{
    (void)timer;
    (void)edata;
    (void)user_ctx;
    BaseType_t hp_woken = pdFALSE;

    if (!s_run.active) {
        return false;
    }

    step_result_t res = (step_result_t)-1;

    if (s_stop_req) {
        res = STEP_RES_STOPPED;
    } else {
        /* Limit switches: active low, valid only while LIMIT_SENSE_EN is
         * high (which it is for the whole run). Debounce = 2 consecutive
         * active reads (docs/architecture.md). */
        if (s_run.cfg.stop_on_open_limit) {
            s_run.open_hits = (gpio_get_level(PIN_LIMIT_OPEN) == 0)
                                  ? (uint8_t)(s_run.open_hits + 1) : 0;
            if (s_run.open_hits >= LIMIT_DEBOUNCE_READS) {
                res = STEP_RES_LIMIT_OPEN;
            }
        }
        if (res == (step_result_t)-1 && s_run.cfg.stop_on_closed_limit) {
            s_run.closed_hits = (gpio_get_level(PIN_LIMIT_CLOSED) == 0)
                                    ? (uint8_t)(s_run.closed_hits + 1) : 0;
            if (s_run.closed_hits >= LIMIT_DEBOUNCE_READS) {
                res = STEP_RES_LIMIT_CLOSED;
            }
        }
        if (res == (step_result_t)-1) {
            if (s_run.steps_taken >= s_run.cfg.target_steps) {
                res = STEP_RES_DONE;
            } else if (s_run.steps_taken >= s_run.cfg.max_steps) {
                res = STEP_RES_WATCHDOG;
            }
        }
    }

    if (res != (step_result_t)-1) {
        s_run.result = res;
        s_run.active = false;
        xSemaphoreGiveFromISR(s_run_done, &hp_woken);
        return hp_woken == pdTRUE;
    }

    /* Advance one half-step. */
    s_run.phase = (uint8_t)((s_run.phase + (s_run.cfg.direction > 0 ? 1 : 7)) & 7);
    coils_write(k_half_step[s_run.phase]);
    s_run.steps_taken++;
    return hp_woken == pdTRUE;
}

/* Execute one stepping run. Caller must hold the rail (motion_rail_acquire)
 * and have LIMIT_SENSE_EN high. Blocks in the motion task; samples the
 * battery under load once per run if requested. */
static step_result_t engine_run(const step_run_cfg_t *cfg, uint32_t rate_hz,
                                int32_t *steps_taken, bool sample_battery,
                                const motion_callbacks_t *cbs)
{
    memset((void *)&s_run, 0, sizeof(s_run));
    s_run.cfg = *cfg;
    s_stop_req = false;
    s_run.result = STEP_RES_DONE;

    if (cfg->target_steps <= 0) {
        if (steps_taken) {
            *steps_taken = 0;
        }
        return STEP_RES_DONE;
    }

    /* Energize the current phase before pacing starts. */
    coils_write(k_half_step[s_run.phase]);
    esp_rom_delay_us(2000); /* first-phase current build-up */

    gptimer_alarm_config_t alarm = {
        .alarm_count = 1000000ULL / rate_hz, /* 1 MHz resolution */
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_raw_count(s_timer, 0));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(s_timer, &alarm));
    s_run.active = true;
    ESP_ERROR_CHECK(gptimer_start(s_timer));

    if (sample_battery && cbs && cbs->on_battery_loaded) {
        /* Wait into the move so the pack sees full motor load, then read
         * the gated divider from task context while the ISR keeps pacing. */
        if (xSemaphoreTake(s_run_done, pdMS_TO_TICKS(BATTERY_SAMPLE_DELAY_MS))
                == pdTRUE) {
            xSemaphoreGive(s_run_done); /* run already ended: re-arm */
        } else {
            uint32_t mv = battery_hw_read_pack_mv();
            if (mv > 0) {
                cbs->on_battery_loaded(mv);
            }
        }
    }

    xSemaphoreTake(s_run_done, portMAX_DELAY);
    ESP_ERROR_CHECK(gptimer_stop(s_timer));
    coils_off(); /* coils never hold position — gear train is non-backdrivable */

    if (steps_taken) {
        *steps_taken = s_run.steps_taken;
    }
    return s_run.result;
}

/* ---- Motion controller -------------------------------------------------- */

typedef enum {
    MOTION_CMD_GOTO = 0,
    MOTION_CMD_RESUME,
    MOTION_CMD_REHOME,
} motion_cmd_type_t;

typedef struct {
    motion_cmd_type_t type;
    uint8_t open_pct;
    motion_origin_t origin;
} motion_cmd_t;

static QueueHandle_t s_cmd_queue;
static motion_callbacks_t s_cbs;
static damper_t s_damper;
static uint8_t s_commanded_open_pct;
static uint8_t s_fault_code; /* FAILSAFE_FAULT_* (motor faults 1/2 only) */
static bool s_needs_home;

uint8_t motion_current_open_pct(void)
{
    return damper_open_pct(&s_damper);
}

uint8_t motion_commanded_open_pct(void)
{
    return s_commanded_open_pct;
}

uint8_t motion_fault_code(void)
{
    return s_fault_code;
}

static void set_motor_fault(uint8_t code)
{
    if (s_fault_code != code) {
        s_fault_code = code;
        if (s_cbs.on_fault) {
            s_cbs.on_fault(code);
        }
    }
}

static void notify_position(bool final)
{
    if (final) {
        nvs_state_set_open_pct(damper_open_pct(&s_damper));
        nvs_state_set_move_pending(false);
    }
    if (s_cbs.on_position) {
        s_cbs.on_position(damper_open_pct(&s_damper), final);
    }
}

/* Home against the CLOSED switch and (re)calibrate range against the OPEN
 * switch when it is unknown. Caller holds the rail + LIMIT_SENSE_EN. */
static bool do_home_locked(void)
{
    int32_t taken = 0;
    step_run_cfg_t cfg;
    int32_t expect = (s_damper.range_steps > 0) ? s_damper.range_steps
                                                : DEFAULT_RANGE_STEPS;

    /* If we are parked on the CLOSED switch, back off first so the seek
     * approaches the switch in a defined direction. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.direction = 1; /* toward open */
    cfg.target_steps = 2 * HOMING_BACKOFF_STEPS;
    cfg.max_steps = 4 * HOMING_BACKOFF_STEPS;
    cfg.stop_on_open_limit = true;
    (void)engine_run(&cfg, HOMING_STEP_RATE_HZ, &taken, false, NULL);

    /* Seek CLOSED at reduced rate; watchdog = expected travel x 1.25. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.direction = -1;
    cfg.target_steps = (expect * 5) / 4 + 4 * HOMING_BACKOFF_STEPS;
    cfg.max_steps = cfg.target_steps;
    cfg.stop_on_closed_limit = true;
    step_result_t res = engine_run(&cfg, HOMING_STEP_RATE_HZ, &taken, false, NULL);
    if (res != STEP_RES_LIMIT_CLOSED) {
        ESP_LOGE(TAG, "homing failed (%d after %ld steps)", res, (long)taken);
        set_motor_fault(FAILSAFE_FAULT_HOMING_TIMEOUT);
        s_damper.position_valid = false;
        return false;
    }

    /* Datum: zero the counter at the switch, then back off. */
    s_damper.position_steps = 0;
    memset(&cfg, 0, sizeof(cfg));
    cfg.direction = 1;
    cfg.target_steps = HOMING_BACKOFF_STEPS;
    cfg.max_steps = 2 * HOMING_BACKOFF_STEPS;
    (void)engine_run(&cfg, HOMING_STEP_RATE_HZ, &taken, false, NULL);
    s_damper.position_steps = taken;
    s_damper.position_valid = true;

    /* Range calibration if not yet known: run to the OPEN switch. */
    if (s_damper.range_steps <= 0) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.direction = 1;
        cfg.target_steps = (DEFAULT_RANGE_STEPS * 5) / 4;
        cfg.max_steps = cfg.target_steps;
        cfg.stop_on_open_limit = true;
        res = engine_run(&cfg, HOMING_STEP_RATE_HZ, &taken, false, NULL);
        if (res != STEP_RES_LIMIT_OPEN) {
            ESP_LOGE(TAG, "range calibration failed (%d)", res);
            set_motor_fault(FAILSAFE_FAULT_HOMING_TIMEOUT);
            s_damper.position_valid = false;
            return false;
        }
        s_damper.range_steps = s_damper.position_steps + taken;
        s_damper.position_steps = s_damper.range_steps;
        nvs_state_set_range_steps(s_damper.range_steps);
        ESP_LOGI(TAG, "range calibrated: %ld half-steps",
                 (long)s_damper.range_steps);
    }

    s_needs_home = false;
    if (s_fault_code == FAILSAFE_FAULT_HOMING_TIMEOUT ||
        s_fault_code == FAILSAFE_FAULT_STALL) {
        set_motor_fault(FAILSAFE_FAULT_NONE); /* recovered */
    }
    return true;
}

static void do_goto_locked(uint8_t target_pct, motion_origin_t origin)
{
    bool fail_open = (origin == MOTION_ORIGIN_FAILSAFE && target_pct == 100);

    if (s_needs_home || !s_damper.position_valid) {
        if (!do_home_locked()) {
            if (fail_open) {
                /* Best effort: blind-drive toward OPEN until the switch or
                 * the watchdog — better stuck open than stuck closed. */
                step_run_cfg_t cfg = {
                    .direction = 1,
                    .target_steps = (DEFAULT_RANGE_STEPS * 5) / 4,
                    .max_steps = (DEFAULT_RANGE_STEPS * 5) / 4,
                    .stop_on_open_limit = true,
                };
                int32_t taken = 0;
                (void)engine_run(&cfg, MOVE_STEP_RATE_HZ, &taken, false, NULL);
            }
            return;
        }
    }

    damper_move_plan_t plan;
    if (!damper_plan_move(&s_damper, target_pct, &plan)) {
        return;
    }
    if (plan.steps == 0) {
        notify_position(true);
        return;
    }

    /* Dirty flag BEFORE moving: a reset mid-move must force re-homing
     * (docs/architecture.md "Position persistence"). */
    nvs_state_set_move_pending(true);

    step_run_cfg_t cfg = {
        .direction = plan.direction,
        .target_steps = plan.steps,
        /* The limit switch in the travel direction is the backstop; the
         * x1.25 watchdog is meaningful for limit-seeking runs. */
        .max_steps = plan.watchdog_steps,
        .stop_on_open_limit = (plan.direction > 0),
        .stop_on_closed_limit = (plan.direction < 0),
    };
    int32_t taken = 0;
    step_result_t res = engine_run(&cfg, MOVE_STEP_RATE_HZ, &taken, true, &s_cbs);

    damper_apply_steps(&s_damper, plan.direction, taken);
    switch (res) {
    case STEP_RES_LIMIT_OPEN:
        s_damper.position_steps = s_damper.range_steps; /* re-datum */
        break;
    case STEP_RES_LIMIT_CLOSED:
        s_damper.position_steps = 0; /* re-datum */
        break;
    case STEP_RES_WATCHDOG:
        set_motor_fault(FAILSAFE_FAULT_STALL);
        s_damper.position_valid = false;
        s_needs_home = true;
        break;
    case STEP_RES_STOPPED:
    case STEP_RES_DONE:
    default:
        break;
    }
    notify_position(true);
}

static void motion_task(void *arg)
{
    (void)arg;

    /* Boot policy: an interrupted move means the step count is untrusted. */
    if (nvs_state_get_move_pending()) {
        ESP_LOGW(TAG, "move was interrupted by reset -> re-home");
        s_needs_home = true;
        s_damper.position_valid = false;
        motion_cmd_t cmd = { .type = MOTION_CMD_REHOME };
        xQueueSend(s_cmd_queue, &cmd, 0);
    }

    motion_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        uint8_t target = cmd.open_pct;
        if (cmd.type == MOTION_CMD_RESUME) {
            target = s_commanded_open_pct;
        }
        bool fail_open = (cmd.origin == MOTION_ORIGIN_FAILSAFE && target == 100);

        /* Fault lockout: after a homing/stall fault only fail-open moves
         * are attempted (docs/architecture.md). */
        if (s_fault_code != FAILSAFE_FAULT_NONE &&
            cmd.type != MOTION_CMD_REHOME && !fail_open) {
            ESP_LOGW(TAG, "move refused, fault_code=%u", s_fault_code);
            notify_position(true); /* resync HA with the real position */
            continue;
        }

        /* Rail + limit-switch bias up for the whole operation. */
        motion_rail_acquire();
        gpio_set_level(PIN_LIMIT_SENSE_EN, 1);

        switch (cmd.type) {
        case MOTION_CMD_REHOME:
            s_needs_home = true;
            if (do_home_locked()) {
                notify_position(true);
            }
            break;
        case MOTION_CMD_GOTO:
        case MOTION_CMD_RESUME:
            do_goto_locked(target, cmd.origin);
            break;
        }

        gpio_set_level(PIN_LIMIT_SENSE_EN, 0);
        motion_rail_release();
    }
}

/* ---- Public API --------------------------------------------------------- */

esp_err_t stepper_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_MOTOR_AIN1) | (1ULL << PIN_MOTOR_AIN2) |
                        (1ULL << PIN_MOTOR_BIN1) | (1ULL << PIN_MOTOR_BIN2) |
                        (1ULL << PIN_MOTOR_PWR_EN) | (1ULL << PIN_LIMIT_SENSE_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    coils_off();
    gpio_set_level(PIN_MOTOR_PWR_EN, 0);
    gpio_set_level(PIN_LIMIT_SENSE_EN, 0);

    /* Limit inputs float when LIMIT_SENSE_EN is low (external pull-ups are
     * gated); keep weak internal pull-ups so reads never oscillate. */
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_LIMIT_OPEN) | (1ULL << PIN_LIMIT_CLOSED),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    s_run_done = xSemaphoreCreateBinary();
    s_rail_mutex = xSemaphoreCreateMutex();

    gptimer_config_t tcfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, /* 1 us ticks */
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&tcfg, &s_timer));
    gptimer_event_callbacks_t cbs = { .on_alarm = step_timer_cb };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(s_timer));
    return ESP_OK;
}

esp_err_t motion_start(const motion_callbacks_t *cbs)
{
    if (cbs) {
        s_cbs = *cbs;
    }

    /* Restore persisted state. Position is only trusted when the previous
     * move completed cleanly (move_pending clear). */
    s_damper.range_steps = nvs_state_get_range_steps(0);
    s_commanded_open_pct = nvs_state_get_open_pct(0);
    if (!nvs_state_get_move_pending() && s_damper.range_steps > 0) {
        s_damper.position_steps =
            damper_pct_to_steps(s_commanded_open_pct, s_damper.range_steps);
        s_damper.position_valid = true;
    } else {
        s_damper.position_valid = false;
        s_needs_home = true;
    }

    s_cmd_queue = xQueueCreate(4, sizeof(motion_cmd_t));
    if (s_cmd_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ok = xTaskCreate(motion_task, "motion", 4096, NULL, 5, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void motion_request_goto_open_pct(uint8_t open_pct, motion_origin_t origin)
{
    open_pct = damper_clamp_pct(open_pct);
    if (origin != MOTION_ORIGIN_FAILSAFE) {
        /* Remember the *commanded* position; fail-open must not overwrite
         * it or resume-after-clear would resume to 100. */
        s_commanded_open_pct = open_pct;
    }
    motion_cmd_t cmd = {
        .type = MOTION_CMD_GOTO,
        .open_pct = open_pct,
        .origin = origin,
    };
    if (s_cmd_queue) {
        xQueueSend(s_cmd_queue, &cmd, 0);
    }
}

void motion_request_resume(void)
{
    motion_cmd_t cmd = {
        .type = MOTION_CMD_RESUME,
        .origin = MOTION_ORIGIN_FAILSAFE,
    };
    if (s_cmd_queue) {
        xQueueSend(s_cmd_queue, &cmd, 0);
    }
}

void motion_request_stop(void)
{
    s_stop_req = true; /* picked up by the step ISR at the next half-step */
    if (s_cmd_queue) {
        xQueueReset(s_cmd_queue); /* drop queued follow-up moves */
    }
}

void motion_request_rehome(void)
{
    motion_cmd_t cmd = { .type = MOTION_CMD_REHOME };
    if (s_cmd_queue) {
        xQueueSend(s_cmd_queue, &cmd, 0);
    }
}
