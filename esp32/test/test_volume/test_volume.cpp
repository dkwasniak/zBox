#include <unity.h>
#include <Arduino.h>
#include <algorithm>
#include "helpers.h"
#include "volume_scale.h"
#define NIGHT_LIGHT_BRIGHTNESS_STEP 10
#define NIGHT_LIGHT_BRIGHTNESS_MIN 10
#define NIGHT_LIGHT_BRIGHTNESS_MAX 100

static int nightLightUp(int current)
{
    return std::min(current + NIGHT_LIGHT_BRIGHTNESS_STEP, NIGHT_LIGHT_BRIGHTNESS_MAX);
}

static int nightLightDown(int current)
{
    return std::max(current - NIGHT_LIGHT_BRIGHTNESS_STEP, NIGHT_LIGHT_BRIGHTNESS_MIN);
}

// Volume tests

void test_volume_level_table_matches_expected_percents()
{
    const uint8_t expected[] = {0, 1, 2, 4, 6, 9, 13, 18, 25, 34, 45, 60, 80};
    for (uint8_t level = OUTPUT_VOL_LEVEL_MIN; level <= OUTPUT_VOL_LEVEL_MAX; ++level) {
        TEST_ASSERT_EQUAL_UINT8(expected[level], volumeLevelToPercent(level));
    }
}

void test_volume_level_up_steps_one_level_and_clamps()
{
    TEST_ASSERT_EQUAL_UINT8(8, stepVolumeLevelUp(7));
    TEST_ASSERT_EQUAL_UINT8(OUTPUT_VOL_LEVEL_MAX, stepVolumeLevelUp(OUTPUT_VOL_LEVEL_MAX));
}

void test_volume_level_down_steps_one_level_and_clamps()
{
    TEST_ASSERT_EQUAL_UINT8(6, stepVolumeLevelDown(7));
    TEST_ASSERT_EQUAL_UINT8(OUTPUT_VOL_LEVEL_MIN, stepVolumeLevelDown(OUTPUT_VOL_LEVEL_MIN));
}

void test_night_light_up_normal()
{
    TEST_ASSERT_EQUAL_INT(60, nightLightUp(50));
}

void test_night_light_up_clamps()
{
    TEST_ASSERT_EQUAL_INT(100, nightLightUp(95));
}

void test_night_light_down_normal()
{
    TEST_ASSERT_EQUAL_INT(40, nightLightDown(50));
}

void test_night_light_down_clamps()
{
    TEST_ASSERT_EQUAL_INT(10, nightLightDown(15));
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

void test_battery_color_names()
{
    TEST_ASSERT_EQUAL_STRING("blue", batteryColorName(5));
    TEST_ASSERT_EQUAL_STRING("green", batteryColorName(4));
    TEST_ASSERT_EQUAL_STRING("yellow", batteryColorName(3));
    TEST_ASSERT_EQUAL_STRING("orange", batteryColorName(2));
    TEST_ASSERT_EQUAL_STRING("red", batteryColorName(1));
}

void setUp() {}
void tearDown() {}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_volume_level_table_matches_expected_percents);
    RUN_TEST(test_volume_level_up_steps_one_level_and_clamps);
    RUN_TEST(test_volume_level_down_steps_one_level_and_clamps);
    RUN_TEST(test_night_light_up_normal);
    RUN_TEST(test_night_light_up_clamps);
    RUN_TEST(test_night_light_down_normal);
    RUN_TEST(test_night_light_down_clamps);
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
    RUN_TEST(test_battery_color_names);
    return UNITY_END();
}
