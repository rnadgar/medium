/* Host unit tests for failsafe.c (pure state machine). Framework: Unity. */
#include "unity.h"

#include "failsafe.h"

void setUp(void) {}
void tearDown(void) {}

/* Convenience: defaults are threshold 150 Pa, hysteresis 30 Pa (clear below
 * 120 Pa), 2 trip samples, 3 clear samples, arm at >= 15 Pa, idle at <= 5 Pa
 * sustained 60 s, auto_clear on. */

static failsafe_t make_default(void)
{
    failsafe_t fs;
    failsafe_init(&fs, NULL);
    return fs;
}

static void feed_until_tripped(failsafe_t *fs, uint16_t pa, uint32_t *t)
{
    failsafe_action_t act;
    do {
        *t += 15000;
        act = failsafe_sample(fs, pa, *t);
    } while (!act.request_open);
}

/* ---------------- defaults / init ---------------- */

static void test_defaults_match_architecture_table(void)
{
    failsafe_config_t cfg = failsafe_default_config();
    TEST_ASSERT_EQUAL_UINT16(150, cfg.threshold_pa);
    TEST_ASSERT_EQUAL_UINT16(30, cfg.clear_hysteresis_pa);
    TEST_ASSERT_TRUE(cfg.auto_clear_enable);
    TEST_ASSERT_EQUAL_UINT8(2, cfg.trip_confirm_samples);

    failsafe_t fs = make_default();
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_IDLE, fs.state);
    TEST_ASSERT_FALSE(fs.tripped);
    TEST_ASSERT_EQUAL_UINT8(FAILSAFE_FAULT_NONE, fs.fault_code);
}

/* ---------------- arming ---------------- */

static void test_arms_when_blower_pressure_rises(void)
{
    failsafe_t fs = make_default();
    failsafe_action_t act = failsafe_sample(&fs, 3, 1000);
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_IDLE, fs.state);
    TEST_ASSERT_FALSE(act.request_open);

    act = failsafe_sample(&fs, 60, 31000); /* blower on */
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_ARMED, fs.state);
    TEST_ASSERT_FALSE(act.request_open);
    TEST_ASSERT_FALSE(act.tripped_changed);
}

static void test_disarms_after_sustained_zero_pressure(void)
{
    failsafe_t fs = make_default();
    failsafe_sample(&fs, 60, 0); /* ARMED */
    /* Low samples, but not yet for idle_disarm_ms (60 s). */
    failsafe_sample(&fs, 0, 30000);
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_ARMED, fs.state);
    failsafe_sample(&fs, 0, 60000);
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_ARMED, fs.state); /* 30 s elapsed low */
    failsafe_sample(&fs, 0, 95000); /* 65 s since first low sample */
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_IDLE, fs.state);
}

static void test_nonzero_pressure_resets_idle_disarm_timer(void)
{
    failsafe_t fs = make_default();
    failsafe_sample(&fs, 60, 0);      /* ARMED */
    failsafe_sample(&fs, 0, 10000);   /* low from t=10 s */
    failsafe_sample(&fs, 40, 40000);  /* blower blip resets the timer */
    failsafe_sample(&fs, 0, 75000);   /* low again from t=75 s */
    failsafe_sample(&fs, 0, 130000);  /* only 55 s low -> still ARMED */
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_ARMED, fs.state);
    failsafe_sample(&fs, 0, 140000);  /* 65 s low -> IDLE */
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_IDLE, fs.state);
}

/* ---------------- tripping ---------------- */

static void test_trip_requires_two_consecutive_samples(void)
{
    failsafe_t fs = make_default();
    failsafe_sample(&fs, 60, 0); /* ARMED */

    failsafe_action_t act = failsafe_sample(&fs, 200, 15000);
    TEST_ASSERT_FALSE(act.request_open); /* 1st sample above: not yet */
    TEST_ASSERT_FALSE(fs.tripped);

    act = failsafe_sample(&fs, 210, 30000); /* 2nd consecutive: trip */
    TEST_ASSERT_TRUE(act.request_open);
    TEST_ASSERT_TRUE(act.tripped_changed);
    TEST_ASSERT_TRUE(fs.tripped);
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_TRIPPED, fs.state);
    TEST_ASSERT_EQUAL_UINT16(210, fs.last_trip_pressure_pa);
}

static void test_single_spike_does_not_trip(void)
{
    failsafe_t fs = make_default();
    failsafe_sample(&fs, 60, 0);
    failsafe_sample(&fs, 200, 15000);         /* spike */
    failsafe_action_t act = failsafe_sample(&fs, 90, 30000); /* back down */
    TEST_ASSERT_FALSE(act.request_open);
    TEST_ASSERT_FALSE(fs.tripped);
    /* Needs two consecutive again. */
    failsafe_sample(&fs, 200, 45000);
    act = failsafe_sample(&fs, 200, 60000);
    TEST_ASSERT_TRUE(act.request_open);
}

static void test_hard_blower_start_trips_from_idle_in_two_samples(void)
{
    failsafe_t fs = make_default();
    /* First sample already above threshold arms AND counts. */
    failsafe_action_t act = failsafe_sample(&fs, 300, 0);
    TEST_ASSERT_FALSE(act.request_open);
    act = failsafe_sample(&fs, 300, 30000);
    TEST_ASSERT_TRUE(act.request_open);
    TEST_ASSERT_TRUE(fs.tripped);
}

static void test_pressure_at_threshold_does_not_trip(void)
{
    failsafe_t fs = make_default();
    failsafe_sample(&fs, 150, 0);  /* == threshold: arms, does not count */
    failsafe_action_t act = failsafe_sample(&fs, 150, 30000);
    TEST_ASSERT_FALSE(act.request_open);
    TEST_ASSERT_FALSE(fs.tripped);
}

/* ---------------- clearing with hysteresis ---------------- */

static void test_clear_needs_pressure_below_threshold_minus_hyst(void)
{
    failsafe_t fs = make_default();
    uint32_t t = 0;
    feed_until_tripped(&fs, 200, &t);

    /* 130 Pa is below threshold but NOT below 150-30=120: no clearing. */
    for (int i = 0; i < 10; i++) {
        t += 15000;
        failsafe_action_t act = failsafe_sample(&fs, 130, t);
        TEST_ASSERT_FALSE(act.request_resume);
        TEST_ASSERT_TRUE(fs.tripped);
    }
}

static void test_clear_after_m_samples_below_clear_level(void)
{
    failsafe_t fs = make_default();
    uint32_t t = 0;
    feed_until_tripped(&fs, 200, &t);

    failsafe_action_t act;
    t += 15000;
    act = failsafe_sample(&fs, 100, t); /* 1 of 3 */
    TEST_ASSERT_FALSE(act.request_resume);
    TEST_ASSERT_TRUE(fs.tripped);
    t += 15000;
    act = failsafe_sample(&fs, 100, t); /* 2 of 3 */
    TEST_ASSERT_FALSE(act.request_resume);
    t += 15000;
    act = failsafe_sample(&fs, 100, t); /* 3 of 3 -> clear */
    TEST_ASSERT_TRUE(act.request_resume);
    TEST_ASSERT_TRUE(act.tripped_changed);
    TEST_ASSERT_FALSE(fs.tripped);
    /* 100 Pa >= arm level -> blower still running -> back to ARMED. */
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_ARMED, fs.state);
}

static void test_clear_counter_resets_on_high_sample(void)
{
    failsafe_t fs = make_default();
    uint32_t t = 0;
    feed_until_tripped(&fs, 200, &t);

    failsafe_action_t act;
    t += 15000; failsafe_sample(&fs, 100, t); /* 1 */
    t += 15000; failsafe_sample(&fs, 100, t); /* 2 */
    t += 15000; failsafe_sample(&fs, 180, t); /* resets counter */
    t += 15000; act = failsafe_sample(&fs, 100, t); /* 1 again */
    TEST_ASSERT_FALSE(act.request_resume);
    t += 15000; act = failsafe_sample(&fs, 100, t); /* 2 */
    TEST_ASSERT_FALSE(act.request_resume);
    t += 15000; act = failsafe_sample(&fs, 100, t); /* 3 -> clear */
    TEST_ASSERT_TRUE(act.request_resume);
}

static void test_clear_to_idle_when_blower_off(void)
{
    failsafe_t fs = make_default();
    uint32_t t = 0;
    feed_until_tripped(&fs, 200, &t);
    for (int i = 0; i < 3; i++) {
        t += 15000;
        failsafe_sample(&fs, 0, t); /* blower died entirely */
    }
    TEST_ASSERT_FALSE(fs.tripped);
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_IDLE, fs.state);
}

static void test_retrip_after_clear(void)
{
    failsafe_t fs = make_default();
    uint32_t t = 0;
    feed_until_tripped(&fs, 200, &t);
    for (int i = 0; i < 3; i++) { t += 15000; failsafe_sample(&fs, 100, t); }
    TEST_ASSERT_FALSE(fs.tripped);

    /* Pressure climbs again -> trips again after 2 samples. */
    t += 15000;
    failsafe_action_t act = failsafe_sample(&fs, 250, t);
    TEST_ASSERT_FALSE(act.request_open);
    t += 15000;
    act = failsafe_sample(&fs, 250, t);
    TEST_ASSERT_TRUE(act.request_open);
    TEST_ASSERT_EQUAL_UINT16(250, fs.last_trip_pressure_pa);
}

/* ---------------- auto_clear latch behavior ---------------- */

static void test_latch_when_auto_clear_disabled(void)
{
    failsafe_config_t cfg = failsafe_default_config();
    cfg.auto_clear_enable = false;
    failsafe_t fs;
    failsafe_init(&fs, &cfg);

    uint32_t t = 0;
    feed_until_tripped(&fs, 200, &t);

    /* Pressure fully recovers for many samples: trip must LATCH. */
    failsafe_action_t act = {0};
    for (int i = 0; i < 10; i++) {
        t += 15000;
        act = failsafe_sample(&fs, 0, t);
        TEST_ASSERT_FALSE(act.request_resume);
        TEST_ASSERT_FALSE(act.tripped_changed);
        TEST_ASSERT_TRUE(fs.tripped);
    }
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_TRIPPED, fs.state);

    /* Manual clear from HA releases the latch and resumes. */
    act = failsafe_manual_clear(&fs);
    TEST_ASSERT_TRUE(act.tripped_changed);
    TEST_ASSERT_TRUE(act.request_resume);
    TEST_ASSERT_FALSE(fs.tripped);
    TEST_ASSERT_EQUAL_INT(FAILSAFE_STATE_IDLE, fs.state);
}

static void test_manual_clear_when_not_tripped_is_noop(void)
{
    failsafe_t fs = make_default();
    failsafe_action_t act = failsafe_manual_clear(&fs);
    TEST_ASSERT_FALSE(act.tripped_changed);
    TEST_ASSERT_FALSE(act.request_resume);
}

static void test_runtime_config_writes(void)
{
    failsafe_t fs = make_default();
    failsafe_set_threshold(&fs, 200);
    failsafe_set_hysteresis(&fs, 50);
    failsafe_set_auto_clear(&fs, false);
    TEST_ASSERT_EQUAL_UINT16(200, fs.cfg.threshold_pa);
    TEST_ASSERT_EQUAL_UINT16(50, fs.cfg.clear_hysteresis_pa);
    TEST_ASSERT_FALSE(fs.cfg.auto_clear_enable);

    /* New threshold governs tripping. */
    failsafe_sample(&fs, 180, 0);      /* arms, 180 < 200: no count */
    failsafe_sample(&fs, 180, 30000);
    TEST_ASSERT_FALSE(fs.tripped);
    failsafe_sample(&fs, 210, 60000);
    failsafe_action_t act = failsafe_sample(&fs, 210, 90000);
    TEST_ASSERT_TRUE(act.request_open);
}

/* ---------------- sensor fault path ---------------- */

static void test_sensor_fault_sets_code_and_fails_open(void)
{
    failsafe_t fs = make_default();
    failsafe_action_t act = failsafe_sensor_fault(&fs, 1000);
    TEST_ASSERT_TRUE(act.fault_changed);
    TEST_ASSERT_TRUE(act.request_open);
    TEST_ASSERT_TRUE(act.tripped_changed);
    TEST_ASSERT_EQUAL_UINT8(FAILSAFE_FAULT_SENSOR, fs.fault_code);
    TEST_ASSERT_TRUE(fs.tripped);

    /* Repeated fault reports are idempotent — no report storm. */
    act = failsafe_sensor_fault(&fs, 31000);
    TEST_ASSERT_FALSE(act.fault_changed);
    TEST_ASSERT_FALSE(act.tripped_changed);
    TEST_ASSERT_FALSE(act.request_open);
}

static void test_sensor_recovery_clears_fault_then_normal_clear_path(void)
{
    failsafe_t fs = make_default();
    failsafe_sensor_fault(&fs, 0);

    /* First good sample clears fault code 3 (reported). */
    failsafe_action_t act = failsafe_sample(&fs, 0, 30000);
    TEST_ASSERT_TRUE(act.fault_changed);
    TEST_ASSERT_EQUAL_UINT8(FAILSAFE_FAULT_NONE, fs.fault_code);
    /* Still tripped until the normal M-sample clear confirms. */
    TEST_ASSERT_TRUE(fs.tripped);

    act = failsafe_sample(&fs, 0, 60000);
    TEST_ASSERT_TRUE(fs.tripped);
    act = failsafe_sample(&fs, 0, 90000); /* 3rd low sample -> clear */
    TEST_ASSERT_TRUE(act.request_resume);
    TEST_ASSERT_FALSE(fs.tripped);
}

static void test_motor_fault_codes_do_not_autoclear(void)
{
    failsafe_t fs = make_default();
    failsafe_action_t act = failsafe_set_motor_fault(&fs, FAILSAFE_FAULT_STALL);
    TEST_ASSERT_TRUE(act.fault_changed);
    TEST_ASSERT_EQUAL_UINT8(FAILSAFE_FAULT_STALL, fs.fault_code);
    /* Good pressure data must NOT clear a motor fault. */
    act = failsafe_sample(&fs, 0, 1000);
    TEST_ASSERT_FALSE(act.fault_changed);
    TEST_ASSERT_EQUAL_UINT8(FAILSAFE_FAULT_STALL, fs.fault_code);
}

/* ---------------- adaptive sampling recommendation ---------------- */

static void test_sample_class_progression(void)
{
    failsafe_t fs = make_default();
    /* Fresh boot: base cadence. */
    TEST_ASSERT_EQUAL_INT(FAILSAFE_SAMPLE_BASE, failsafe_sample_class(&fs));

    failsafe_sample(&fs, 60, 0); /* ARMED -> active cadence */
    TEST_ASSERT_EQUAL_INT(FAILSAFE_SAMPLE_ACTIVE, failsafe_sample_class(&fs));

    /* Blower off long enough -> IDLE -> slow cadence. */
    failsafe_sample(&fs, 0, 10000);
    failsafe_sample(&fs, 0, 80000);
    TEST_ASSERT_EQUAL_INT(FAILSAFE_SAMPLE_IDLE, failsafe_sample_class(&fs));

    /* Tripped -> active. */
    failsafe_sample(&fs, 300, 100000);
    failsafe_sample(&fs, 300, 115000);
    TEST_ASSERT_TRUE(fs.tripped);
    TEST_ASSERT_EQUAL_INT(FAILSAFE_SAMPLE_ACTIVE, failsafe_sample_class(&fs));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_match_architecture_table);
    RUN_TEST(test_arms_when_blower_pressure_rises);
    RUN_TEST(test_disarms_after_sustained_zero_pressure);
    RUN_TEST(test_nonzero_pressure_resets_idle_disarm_timer);
    RUN_TEST(test_trip_requires_two_consecutive_samples);
    RUN_TEST(test_single_spike_does_not_trip);
    RUN_TEST(test_hard_blower_start_trips_from_idle_in_two_samples);
    RUN_TEST(test_pressure_at_threshold_does_not_trip);
    RUN_TEST(test_clear_needs_pressure_below_threshold_minus_hyst);
    RUN_TEST(test_clear_after_m_samples_below_clear_level);
    RUN_TEST(test_clear_counter_resets_on_high_sample);
    RUN_TEST(test_clear_to_idle_when_blower_off);
    RUN_TEST(test_retrip_after_clear);
    RUN_TEST(test_latch_when_auto_clear_disabled);
    RUN_TEST(test_manual_clear_when_not_tripped_is_noop);
    RUN_TEST(test_runtime_config_writes);
    RUN_TEST(test_sensor_fault_sets_code_and_fails_open);
    RUN_TEST(test_sensor_recovery_clears_fault_then_normal_clear_path);
    RUN_TEST(test_motor_fault_codes_do_not_autoclear);
    RUN_TEST(test_sample_class_progression);
    return UNITY_END();
}
