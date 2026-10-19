/*
 * Host unit tests for components/app_logic/weather_display_math: wind arrow
 * angle, compass index and rain-sum text, with the values the design's
 * JavaScript produces.
 */

#include "unity.h"
#include "weather_display_math.h"

#include <math.h>
#include <string.h>

static void test_arrow_points_where_the_wind_goes(void) {
    TEST_ASSERT_EQUAL_INT(180, wdm_arrow_deg(0));   /* from the north: arrow down */
    TEST_ASSERT_EQUAL_INT(270, wdm_arrow_deg(90));  /* from the east: arrow left  */
    TEST_ASSERT_EQUAL_INT(0, wdm_arrow_deg(180));
    TEST_ASSERT_EQUAL_INT(45, wdm_arrow_deg(225));
    TEST_ASSERT_EQUAL_INT(179, wdm_arrow_deg(359));
}

static void test_arrow_rounds_and_wraps(void) {
    TEST_ASSERT_EQUAL_INT(180, wdm_arrow_deg(360));
    TEST_ASSERT_EQUAL_INT(181, wdm_arrow_deg(0.5f)); /* Math.round(180.5) = 181 */
    TEST_ASSERT_EQUAL_INT(170, wdm_arrow_deg(-10));  /* normalized to 350 */
    TEST_ASSERT_EQUAL_INT(0, wdm_arrow_deg(NAN));
}

static void test_compass_index_covers_eight_points(void) {
    TEST_ASSERT_EQUAL_INT(0, wdm_compass_index(0));
    TEST_ASSERT_EQUAL_INT(0, wdm_compass_index(22.4f));
    TEST_ASSERT_EQUAL_INT(1, wdm_compass_index(22.5f)); /* Math.round(0.5) = 1 */
    TEST_ASSERT_EQUAL_INT(2, wdm_compass_index(90));
    TEST_ASSERT_EQUAL_INT(4, wdm_compass_index(180));
    TEST_ASSERT_EQUAL_INT(5, wdm_compass_index(225));
    TEST_ASSERT_EQUAL_INT(6, wdm_compass_index(270));
    TEST_ASSERT_EQUAL_INT(7, wdm_compass_index(315));
    TEST_ASSERT_EQUAL_INT(0, wdm_compass_index(350)); /* wraps back to north */
}

static void test_compass_index_normalizes_odd_input(void) {
    TEST_ASSERT_EQUAL_INT(2, wdm_compass_index(450));  /* 360 + 90 */
    TEST_ASSERT_EQUAL_INT(6, wdm_compass_index(-90));
    TEST_ASSERT_EQUAL_INT(0, wdm_compass_index(NAN));
}

static void test_precip_text_has_one_decimal_below_ten_mm(void) {
    char b[16];
    wdm_format_precip_mm(b, sizeof b, 0.0f);
    TEST_ASSERT_EQUAL_STRING("0.0 mm", b);
    wdm_format_precip_mm(b, sizeof b, 0.42f);
    TEST_ASSERT_EQUAL_STRING("0.4 mm", b);
    wdm_format_precip_mm(b, sizeof b, 9.94f);
    TEST_ASSERT_EQUAL_STRING("9.9 mm", b);
}

static void test_precip_text_rounds_from_ten_mm(void) {
    char b[16];
    wdm_format_precip_mm(b, sizeof b, 10.0f);
    TEST_ASSERT_EQUAL_STRING("10 mm", b);
    wdm_format_precip_mm(b, sizeof b, 12.6f);
    TEST_ASSERT_EQUAL_STRING("13 mm", b);
    wdm_format_precip_mm(b, sizeof b, 12.5f);
    TEST_ASSERT_EQUAL_STRING("13 mm", b); /* Math.round(12.5) = 13 */
}

static void test_precip_text_survives_bad_input_and_small_buffers(void) {
    char b[16];
    wdm_format_precip_mm(b, sizeof b, -3.0f);
    TEST_ASSERT_EQUAL_STRING("0.0 mm", b);
    wdm_format_precip_mm(b, sizeof b, NAN);
    TEST_ASSERT_EQUAL_STRING("0.0 mm", b);
    char tiny[4];
    wdm_format_precip_mm(tiny, sizeof tiny, 12.0f);
    TEST_ASSERT_EQUAL_UINT(3, strlen(tiny)); /* truncated but terminated */
}

static void test_night_is_only_is_day_zero(void) {
    TEST_ASSERT_TRUE(wdm_is_night(0));
    TEST_ASSERT_FALSE(wdm_is_night(1));
    TEST_ASSERT_FALSE(wdm_is_night(-1));
}

void test_weather_display_math_run(void) {
    RUN_TEST(test_arrow_points_where_the_wind_goes);
    RUN_TEST(test_arrow_rounds_and_wraps);
    RUN_TEST(test_compass_index_covers_eight_points);
    RUN_TEST(test_compass_index_normalizes_odd_input);
    RUN_TEST(test_precip_text_has_one_decimal_below_ten_mm);
    RUN_TEST(test_precip_text_rounds_from_ten_mm);
    RUN_TEST(test_precip_text_survives_bad_input_and_small_buffers);
    RUN_TEST(test_night_is_only_is_day_zero);
}
