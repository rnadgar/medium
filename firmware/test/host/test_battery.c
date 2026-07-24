/* Host unit tests for battery.c (pure LiFeS2 curves). Framework: Unity. */
#include "unity.h"

#include "battery.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------- loaded curve ---------------- */

static void test_loaded_endpoints(void)
{
    /* Fresh pack and above: pegged at 100. */
    TEST_ASSERT_EQUAL_UINT8(100, battery_percent_from_loaded_mv(6400));
    TEST_ASSERT_EQUAL_UINT8(100, battery_percent_from_loaded_mv(7200));
    /* Cutoff and below: 0. */
    TEST_ASSERT_EQUAL_UINT8(0, battery_percent_from_loaded_mv(4000));
    TEST_ASSERT_EQUAL_UINT8(0, battery_percent_from_loaded_mv(3000));
    TEST_ASSERT_EQUAL_UINT8(0, battery_percent_from_loaded_mv(0));
}

static void test_loaded_knot_points(void)
{
    TEST_ASSERT_EQUAL_UINT8(95, battery_percent_from_loaded_mv(6000));
    TEST_ASSERT_EQUAL_UINT8(55, battery_percent_from_loaded_mv(5400));
    TEST_ASSERT_EQUAL_UINT8(25, battery_percent_from_loaded_mv(5000));
    TEST_ASSERT_EQUAL_UINT8(10, battery_percent_from_loaded_mv(4700));
}

static void test_loaded_interpolation_midpoints(void)
{
    /* Midway 5400 (55 %) .. 5600 (70 %): 5500 -> ~62-63 %. */
    uint8_t p = battery_percent_from_loaded_mv(5500);
    TEST_ASSERT_TRUE(p >= 62 && p <= 63);
    /* Midway 5000 (25 %) .. 5200 (40 %): 5100 -> ~32-33 %. */
    p = battery_percent_from_loaded_mv(5100);
    TEST_ASSERT_TRUE(p >= 32 && p <= 33);
}

static void test_loaded_monotonic_full_sweep(void)
{
    /* Percent must never decrease as voltage increases, 1 mV steps. */
    uint8_t prev = battery_percent_from_loaded_mv(3500);
    for (uint32_t mv = 3501; mv <= 7500; mv++) {
        uint8_t p = battery_percent_from_loaded_mv(mv);
        TEST_ASSERT_TRUE_MESSAGE(p >= prev, "loaded curve not monotonic");
        TEST_ASSERT_TRUE(p <= 100);
        prev = p;
    }
    TEST_ASSERT_EQUAL_UINT8(100, prev);
}

/* ---------------- unloaded curve ---------------- */

static void test_unloaded_endpoints_and_monotonic(void)
{
    TEST_ASSERT_EQUAL_UINT8(100, battery_percent_from_unloaded_mv(7000));
    TEST_ASSERT_EQUAL_UINT8(100, battery_percent_from_unloaded_mv(7300));
    TEST_ASSERT_EQUAL_UINT8(0, battery_percent_from_unloaded_mv(4400));
    TEST_ASSERT_EQUAL_UINT8(0, battery_percent_from_unloaded_mv(1000));

    uint8_t prev = battery_percent_from_unloaded_mv(4000);
    for (uint32_t mv = 4001; mv <= 7400; mv += 1) {
        uint8_t p = battery_percent_from_unloaded_mv(mv);
        TEST_ASSERT_TRUE_MESSAGE(p >= prev, "unloaded curve not monotonic");
        prev = p;
    }
}

static void test_unloaded_sits_above_loaded(void)
{
    /* For the same true state of charge the unloaded voltage is higher, so
     * at a given mid-range voltage the unloaded curve must report LESS
     * remaining than the loaded curve (sanity of table orientation). */
    for (uint32_t mv = 5000; mv <= 6300; mv += 100) {
        TEST_ASSERT_TRUE(battery_percent_from_unloaded_mv(mv) <=
                         battery_percent_from_loaded_mv(mv));
    }
}

/* ---------------- ZCL encodings ---------------- */

static void test_zcl_voltage_encoding(void)
{
    TEST_ASSERT_EQUAL_UINT8(60, battery_zcl_voltage_from_mv(6000)); /* 100 mV units */
    TEST_ASSERT_EQUAL_UINT8(60, battery_zcl_voltage_from_mv(6049)); /* rounds down */
    TEST_ASSERT_EQUAL_UINT8(61, battery_zcl_voltage_from_mv(6050)); /* rounds up */
    TEST_ASSERT_EQUAL_UINT8(0, battery_zcl_voltage_from_mv(0));
    TEST_ASSERT_EQUAL_UINT8(0xFE, battery_zcl_voltage_from_mv(60000)); /* clamp < 0xFF invalid */
}

static void test_zcl_percentage_encoding(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, battery_zcl_percentage_from_percent(0));
    TEST_ASSERT_EQUAL_UINT8(100, battery_zcl_percentage_from_percent(50));
    TEST_ASSERT_EQUAL_UINT8(200, battery_zcl_percentage_from_percent(100));
    TEST_ASSERT_EQUAL_UINT8(200, battery_zcl_percentage_from_percent(150)); /* clamp */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_loaded_endpoints);
    RUN_TEST(test_loaded_knot_points);
    RUN_TEST(test_loaded_interpolation_midpoints);
    RUN_TEST(test_loaded_monotonic_full_sweep);
    RUN_TEST(test_unloaded_endpoints_and_monotonic);
    RUN_TEST(test_unloaded_sits_above_loaded);
    RUN_TEST(test_zcl_voltage_encoding);
    RUN_TEST(test_zcl_percentage_encoding);
    return UNITY_END();
}
