#include <unity.h>
#include <Arduino.h>
#include <algorithm>
#include "helpers.h"

// Constants from musicbox_config.h (local copies for pure-logic tests)
#define BT_VOL_STEP 5
#define BT_VOL_MIN  0
#define BT_VOL_MAX  100

// Pure volume logic (math only, no hardware side effects)
static int volumeUp(int current)
{
    return std::min(current + BT_VOL_STEP, BT_VOL_MAX);
}

static int volumeDown(int current)
{
    return std::max(current - BT_VOL_STEP, BT_VOL_MIN);
}

// Volume tests

void test_vol_up_normal()
{
    TEST_ASSERT_EQUAL_INT(55, volumeUp(50));
}

void test_vol_up_clamps_at_100()
{
    TEST_ASSERT_EQUAL_INT(100, volumeUp(100));
}

void test_vol_near_max()
{
    TEST_ASSERT_EQUAL_INT(100, volumeUp(98));
}

void test_vol_down_normal()
{
    TEST_ASSERT_EQUAL_INT(45, volumeDown(50));
}

void test_vol_down_clamps_at_0()
{
    TEST_ASSERT_EQUAL_INT(0, volumeDown(0));
}

void test_vol_near_min()
{
    TEST_ASSERT_EQUAL_INT(0, volumeDown(2));
}

// Battery tests

void test_battery_5bars()
{
    TEST_ASSERT_EQUAL_INT(5, batteryBars(4.20f));
}

void test_battery_5bars_edge()
{
    TEST_ASSERT_EQUAL_INT(5, batteryBars(4.05f));
}

void test_battery_4bars()
{
    TEST_ASSERT_EQUAL_INT(4, batteryBars(3.95f));
}

void test_battery_4bars_edge()
{
    TEST_ASSERT_EQUAL_INT(4, batteryBars(3.90f));
}

void test_battery_3bars()
{
    TEST_ASSERT_EQUAL_INT(3, batteryBars(3.85f));
}

void test_battery_3bars_edge()
{
    TEST_ASSERT_EQUAL_INT(3, batteryBars(3.80f));
}

void test_battery_2bars()
{
    TEST_ASSERT_EQUAL_INT(2, batteryBars(3.75f));
}

void test_battery_2bars_edge()
{
    TEST_ASSERT_EQUAL_INT(2, batteryBars(3.70f));
}

void test_battery_1bar()
{
    TEST_ASSERT_EQUAL_INT(1, batteryBars(3.50f));
}

void test_battery_boundary_below_390()
{
    // 3.899 < 3.90 → 3 bary, nie 4
    TEST_ASSERT_EQUAL_INT(3, batteryBars(3.899f));
}

void setUp() {}
void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_vol_up_normal);
    RUN_TEST(test_vol_up_clamps_at_100);
    RUN_TEST(test_vol_near_max);
    RUN_TEST(test_vol_down_normal);
    RUN_TEST(test_vol_down_clamps_at_0);
    RUN_TEST(test_vol_near_min);
    RUN_TEST(test_battery_5bars);
    RUN_TEST(test_battery_5bars_edge);
    RUN_TEST(test_battery_4bars);
    RUN_TEST(test_battery_4bars_edge);
    RUN_TEST(test_battery_3bars);
    RUN_TEST(test_battery_3bars_edge);
    RUN_TEST(test_battery_2bars);
    RUN_TEST(test_battery_2bars_edge);
    RUN_TEST(test_battery_1bar);
    RUN_TEST(test_battery_boundary_below_390);
    return UNITY_END();
}
