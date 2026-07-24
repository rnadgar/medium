/* Host unit tests for damper.c (pure logic). Framework: vendored Unity. */
#include "unity.h"

#include "damper.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------- lift <-> open inversion ---------------- */

static void test_open_to_lift_basics(void)
{
    TEST_ASSERT_EQUAL_UINT8(100, damper_open_to_lift(0));   /* closed -> lift 100 */
    TEST_ASSERT_EQUAL_UINT8(0, damper_open_to_lift(100));   /* open -> lift 0 */
    TEST_ASSERT_EQUAL_UINT8(50, damper_open_to_lift(50));
    TEST_ASSERT_EQUAL_UINT8(25, damper_open_to_lift(75));
}

static void test_lift_to_open_basics(void)
{
    TEST_ASSERT_EQUAL_UINT8(100, damper_lift_to_open(0));
    TEST_ASSERT_EQUAL_UINT8(0, damper_lift_to_open(100));
    TEST_ASSERT_EQUAL_UINT8(70, damper_lift_to_open(30));
}

static void test_inversion_round_trip_full_sweep(void)
{
    for (int p = 0; p <= 100; p++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)p,
            damper_lift_to_open(damper_open_to_lift((uint8_t)p)));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)p,
            damper_open_to_lift(damper_lift_to_open((uint8_t)p)));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(100 - p),
            damper_open_to_lift((uint8_t)p));
    }
}

static void test_inversion_clamps_out_of_range(void)
{
    /* ZCL "invalid" 0xFF and anything > 100 clamps to 100 first. */
    TEST_ASSERT_EQUAL_UINT8(0, damper_open_to_lift(255));
    TEST_ASSERT_EQUAL_UINT8(0, damper_lift_to_open(255));
    TEST_ASSERT_EQUAL_UINT8(0, damper_open_to_lift(101));
}

static void test_clamp_pct(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, damper_clamp_pct(-5));
    TEST_ASSERT_EQUAL_UINT8(0, damper_clamp_pct(0));
    TEST_ASSERT_EQUAL_UINT8(42, damper_clamp_pct(42));
    TEST_ASSERT_EQUAL_UINT8(100, damper_clamp_pct(100));
    TEST_ASSERT_EQUAL_UINT8(100, damper_clamp_pct(101));
    TEST_ASSERT_EQUAL_UINT8(100, damper_clamp_pct(100000));
}

/* ---------------- percent <-> step math ---------------- */

static void test_pct_to_steps_endpoints(void)
{
    TEST_ASSERT_EQUAL_INT32(0, damper_pct_to_steps(0, 1365));
    TEST_ASSERT_EQUAL_INT32(1365, damper_pct_to_steps(100, 1365));
    TEST_ASSERT_EQUAL_INT32(683, damper_pct_to_steps(50, 1365)); /* 682.5 -> 683 */
}

static void test_pct_to_steps_rounding(void)
{
    /* 1 % of 150 = 1.5 -> rounds to 2 */
    TEST_ASSERT_EQUAL_INT32(2, damper_pct_to_steps(1, 150));
    /* 33 % of 100 = 33 exactly */
    TEST_ASSERT_EQUAL_INT32(33, damper_pct_to_steps(33, 100));
}

static void test_pct_to_steps_uncalibrated(void)
{
    TEST_ASSERT_EQUAL_INT32(0, damper_pct_to_steps(50, 0));
    TEST_ASSERT_EQUAL_INT32(0, damper_pct_to_steps(50, -10));
}

static void test_steps_to_pct_endpoints_and_clamp(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, damper_steps_to_pct(0, 1365));
    TEST_ASSERT_EQUAL_UINT8(100, damper_steps_to_pct(1365, 1365));
    TEST_ASSERT_EQUAL_UINT8(100, damper_steps_to_pct(2000, 1365)); /* over-range */
    TEST_ASSERT_EQUAL_UINT8(0, damper_steps_to_pct(-5, 1365));     /* under-range */
    TEST_ASSERT_EQUAL_UINT8(0, damper_steps_to_pct(100, 0));       /* uncalibrated */
}

static void test_step_pct_round_trip_various_ranges(void)
{
    const int32_t ranges[] = { 100, 150, 1365, 4096, 12288 };
    for (size_t r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++) {
        for (int p = 0; p <= 100; p++) {
            int32_t steps = damper_pct_to_steps((uint8_t)p, ranges[r]);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)p,
                damper_steps_to_pct(steps, ranges[r]));
        }
    }
}

static void test_steps_to_pct_monotonic(void)
{
    const int32_t range = 1365;
    uint8_t prev = 0;
    for (int32_t s = 0; s <= range; s++) {
        uint8_t p = damper_steps_to_pct(s, range);
        TEST_ASSERT_TRUE(p >= prev);
        prev = p;
    }
    TEST_ASSERT_EQUAL_UINT8(100, prev);
}

/* ---------------- move planning ---------------- */

static void test_plan_move_requires_valid_position(void)
{
    damper_t d = { .range_steps = 1365, .position_steps = 0,
                   .position_valid = false };
    damper_move_plan_t plan;
    TEST_ASSERT_FALSE(damper_plan_move(&d, 100, &plan));
    TEST_ASSERT_EQUAL_INT32(0, plan.steps);

    d.position_valid = true;
    d.range_steps = 0; /* not calibrated */
    TEST_ASSERT_FALSE(damper_plan_move(&d, 100, &plan));
}

static void test_plan_move_full_open(void)
{
    damper_t d = { .range_steps = 1365, .position_steps = 0,
                   .position_valid = true };
    damper_move_plan_t plan;
    TEST_ASSERT_TRUE(damper_plan_move(&d, 100, &plan));
    TEST_ASSERT_EQUAL_INT8(1, plan.direction);
    TEST_ASSERT_EQUAL_INT32(1365, plan.steps);
    TEST_ASSERT_EQUAL_INT32(1365, plan.target_steps);
    /* watchdog = ceil(1365 * 1.25) = ceil(1706.25) = 1707 */
    TEST_ASSERT_EQUAL_INT32(1707, plan.watchdog_steps);
}

static void test_plan_move_toward_closed(void)
{
    damper_t d = { .range_steps = 1000, .position_steps = 750,
                   .position_valid = true };
    damper_move_plan_t plan;
    TEST_ASSERT_TRUE(damper_plan_move(&d, 25, &plan));
    TEST_ASSERT_EQUAL_INT8(-1, plan.direction);
    TEST_ASSERT_EQUAL_INT32(500, plan.steps);
    TEST_ASSERT_EQUAL_INT32(250, plan.target_steps);
    TEST_ASSERT_EQUAL_INT32(625, plan.watchdog_steps); /* 500*1.25 exact */
}

static void test_plan_move_no_op(void)
{
    damper_t d = { .range_steps = 1000, .position_steps = 500,
                   .position_valid = true };
    damper_move_plan_t plan;
    TEST_ASSERT_TRUE(damper_plan_move(&d, 50, &plan));
    TEST_ASSERT_EQUAL_INT8(0, plan.direction);
    TEST_ASSERT_EQUAL_INT32(0, plan.steps);
    TEST_ASSERT_EQUAL_INT32(0, plan.watchdog_steps);
}

static void test_watchdog_is_ceil_of_1_25x(void)
{
    damper_t d = { .range_steps = 10000, .position_steps = 0,
                   .position_valid = true };
    damper_move_plan_t plan;
    for (int p = 1; p <= 100; p++) {
        d.position_steps = 0;
        TEST_ASSERT_TRUE(damper_plan_move(&d, (uint8_t)p, &plan));
        int64_t expect = (plan.steps * 5 + 3) / 4; /* ceil(steps*1.25) */
        TEST_ASSERT_EQUAL_INT32((int32_t)expect, plan.watchdog_steps);
        TEST_ASSERT_TRUE(plan.watchdog_steps >= plan.steps);
    }
}

/* ---------------- position bookkeeping ---------------- */

static void test_apply_steps_and_clamping(void)
{
    damper_t d = { .range_steps = 1000, .position_steps = 500,
                   .position_valid = true };
    damper_apply_steps(&d, 1, 100);
    TEST_ASSERT_EQUAL_INT32(600, d.position_steps);
    damper_apply_steps(&d, -1, 700); /* would go below 0 -> clamps */
    TEST_ASSERT_EQUAL_INT32(0, d.position_steps);
    damper_apply_steps(&d, 1, 5000); /* clamps at range */
    TEST_ASSERT_EQUAL_INT32(1000, d.position_steps);
    damper_apply_steps(&d, 0, 123);  /* no direction -> no change */
    TEST_ASSERT_EQUAL_INT32(1000, d.position_steps);
    damper_apply_steps(&d, 1, -50);  /* negative count ignored */
    TEST_ASSERT_EQUAL_INT32(1000, d.position_steps);
}

static void test_open_pct_accessor(void)
{
    damper_t d = { .range_steps = 1000, .position_steps = 250,
                   .position_valid = true };
    TEST_ASSERT_EQUAL_UINT8(25, damper_open_pct(&d));
    d.position_valid = false;
    TEST_ASSERT_EQUAL_UINT8(0, damper_open_pct(&d));
    TEST_ASSERT_EQUAL_UINT8(0, damper_open_pct(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_to_lift_basics);
    RUN_TEST(test_lift_to_open_basics);
    RUN_TEST(test_inversion_round_trip_full_sweep);
    RUN_TEST(test_inversion_clamps_out_of_range);
    RUN_TEST(test_clamp_pct);
    RUN_TEST(test_pct_to_steps_endpoints);
    RUN_TEST(test_pct_to_steps_rounding);
    RUN_TEST(test_pct_to_steps_uncalibrated);
    RUN_TEST(test_steps_to_pct_endpoints_and_clamp);
    RUN_TEST(test_step_pct_round_trip_various_ranges);
    RUN_TEST(test_steps_to_pct_monotonic);
    RUN_TEST(test_plan_move_requires_valid_position);
    RUN_TEST(test_plan_move_full_open);
    RUN_TEST(test_plan_move_toward_closed);
    RUN_TEST(test_plan_move_no_op);
    RUN_TEST(test_watchdog_is_ceil_of_1_25x);
    RUN_TEST(test_apply_steps_and_clamping);
    RUN_TEST(test_open_pct_accessor);
    return UNITY_END();
}
