/*
 * Host unit tests for components/app_logic/screensaver_policy.
 * Runs natively on the Linux target. This is the test path for the screen
 * saver that does not wait for the real timeout: the inactivity time is a
 * plain number here, and ms_per_unit shrinks the unit the same way the
 * CONFIG_WEATHER_SCREENSAVER_FAST_TEST firmware build does.
 */

#include "unity.h"
#include "screensaver_policy.h"


#define MIN_MS SAVER_MS_PER_MINUTE

static void test_never_starts_when_off(void) {
    screensaver_policy_t p;
    screensaver_policy_init(&p);
    TEST_ASSERT_EQUAL_INT(SAVER_EVENT_NONE, screensaver_policy_tick(&p, 0, UINT32_MAX, MIN_MS));
    TEST_ASSERT_FALSE(p.active);
}

static void test_starts_exactly_at_the_threshold(void) {
    screensaver_policy_t p;
    screensaver_policy_init(&p);
    TEST_ASSERT_EQUAL_INT(SAVER_EVENT_NONE, screensaver_policy_tick(&p, 15, 15 * MIN_MS - 1, MIN_MS));
    TEST_ASSERT_EQUAL_INT(SAVER_EVENT_START, screensaver_policy_tick(&p, 15, 15 * MIN_MS, MIN_MS));
    TEST_ASSERT_TRUE(p.active);
}

static void test_start_fires_only_once(void) {
    screensaver_policy_t p;
    screensaver_policy_init(&p);
    screensaver_policy_tick(&p, 15, 20 * MIN_MS, MIN_MS);
    TEST_ASSERT_EQUAL_INT(SAVER_EVENT_NONE, screensaver_policy_tick(&p, 15, 21 * MIN_MS, MIN_MS));
}

static void test_a_tap_stops_it_and_restarts_the_countdown(void) {
    screensaver_policy_t p;
    screensaver_policy_init(&p);
    screensaver_policy_tick(&p, 15, 15 * MIN_MS, MIN_MS);
    TEST_ASSERT_EQUAL_INT(SAVER_EVENT_STOP, screensaver_policy_on_touch(&p));
    TEST_ASSERT_FALSE(p.active);
    /* LVGL resets its inactivity time on the tap, so the next tick sees ~0. */
    TEST_ASSERT_EQUAL_INT(SAVER_EVENT_NONE, screensaver_policy_tick(&p, 15, 1000, MIN_MS));
    TEST_ASSERT_EQUAL_INT(SAVER_EVENT_START, screensaver_policy_tick(&p, 15, 15 * MIN_MS, MIN_MS));
}

static void test_a_tap_while_inactive_does_nothing(void) {
    screensaver_policy_t p;
    screensaver_policy_init(&p);
    TEST_ASSERT_EQUAL_INT(SAVER_EVENT_NONE, screensaver_policy_on_touch(&p));
}

static void test_fast_test_scale_counts_the_setting_in_seconds(void) {
    screensaver_policy_t p;
    screensaver_policy_init(&p);
    TEST_ASSERT_EQUAL_INT(SAVER_EVENT_NONE, screensaver_policy_tick(&p, 15, 14999, SAVER_MS_PER_SECOND));
    TEST_ASSERT_EQUAL_INT(SAVER_EVENT_START, screensaver_policy_tick(&p, 15, 15000, SAVER_MS_PER_SECOND));
}

static void test_switching_the_setting_off_ends_a_running_saver(void) {
    screensaver_policy_t p;
    screensaver_policy_init(&p);
    screensaver_policy_tick(&p, 15, 15 * MIN_MS, MIN_MS);
    TEST_ASSERT_EQUAL_INT(SAVER_EVENT_NONE, screensaver_policy_setting_changed(&p, 30));
    TEST_ASSERT_TRUE(p.active);
    TEST_ASSERT_EQUAL_INT(SAVER_EVENT_STOP, screensaver_policy_setting_changed(&p, 0));
    TEST_ASSERT_FALSE(p.active);
}

static void test_option_tables_are_valid(void) {
    for (int i = 0; i < SAVER_TIMEOUT_OPTION_COUNT; i++)
        TEST_ASSERT_TRUE(saver_timeout_valid(k_saver_timeout_minutes[i]));
    TEST_ASSERT_FALSE(saver_timeout_valid(5));
    TEST_ASSERT_EQUAL_INT(0, k_saver_timeout_minutes[SAVER_TIMEOUT_DEFAULT_IDX]);
    TEST_ASSERT_TRUE(saver_style_valid(SAVER_STYLE_ORANGE));
    TEST_ASSERT_FALSE(saver_style_valid(SAVER_STYLE_COUNT));
    TEST_ASSERT_FALSE(saver_style_valid(-1));
}

static void test_digits_24h(void) {
    uint8_t d[SAVER_DIGIT_COUNT];
    screensaver_split_digits(7, 5, 9, false, d);
    const uint8_t want[SAVER_DIGIT_COUNT] = { 0, 7, 0, 5, 0, 9 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, d, SAVER_DIGIT_COUNT);
    screensaver_split_digits(23, 59, 59, false, d);
    const uint8_t want2[SAVER_DIGIT_COUNT] = { 2, 3, 5, 9, 5, 9 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want2, d, SAVER_DIGIT_COUNT);
}

static void test_digits_12h_midnight_noon_and_afternoon(void) {
    uint8_t d[SAVER_DIGIT_COUNT];
    screensaver_split_digits(0, 0, 0, true, d);
    TEST_ASSERT_EQUAL_UINT8(1, d[0]);
    TEST_ASSERT_EQUAL_UINT8(2, d[1]);
    screensaver_split_digits(12, 30, 0, true, d);
    TEST_ASSERT_EQUAL_UINT8(1, d[0]);
    TEST_ASSERT_EQUAL_UINT8(2, d[1]);
    screensaver_split_digits(15, 30, 0, true, d);
    TEST_ASSERT_EQUAL_UINT8(0, d[0]);
    TEST_ASSERT_EQUAL_UINT8(3, d[1]);
}

static void test_drift_stays_in_bounds_and_alternates_layout(void) {
    bool saw_above = false, saw_below = false;
    for (int64_t t = 0; t < 3 * 24 * 3600; t += 60) {
        saver_drift_t d = screensaver_drift(t);
        TEST_ASSERT_TRUE(d.dx >= -28 && d.dx <= 28);
        TEST_ASSERT_TRUE(d.dy >= -14 && d.dy <= 14);
        if (d.date_above) saw_above = true; else saw_below = true;
    }
    TEST_ASSERT_TRUE(saw_above);
    TEST_ASSERT_TRUE(saw_below);
}

static void test_drift_moves_and_swap_period_is_ten_minutes(void) {
    saver_drift_t a = screensaver_drift(0);
    TEST_ASSERT_EQUAL_INT(0, a.dx);
    TEST_ASSERT_EQUAL_INT(0, a.dy);
    TEST_ASSERT_FALSE(a.date_above);
    TEST_ASSERT_FALSE(screensaver_drift(599).date_above);
    TEST_ASSERT_TRUE(screensaver_drift(600).date_above);
    TEST_ASSERT_FALSE(screensaver_drift(1200).date_above);
    /* a quarter of the 37-minute period is near the positive peak */
    TEST_ASSERT_TRUE(screensaver_drift(9 * 60).dx > 20);
}

void test_screensaver_policy_run(void) {
    RUN_TEST(test_never_starts_when_off);
    RUN_TEST(test_starts_exactly_at_the_threshold);
    RUN_TEST(test_start_fires_only_once);
    RUN_TEST(test_a_tap_stops_it_and_restarts_the_countdown);
    RUN_TEST(test_a_tap_while_inactive_does_nothing);
    RUN_TEST(test_fast_test_scale_counts_the_setting_in_seconds);
    RUN_TEST(test_switching_the_setting_off_ends_a_running_saver);
    RUN_TEST(test_option_tables_are_valid);
    RUN_TEST(test_digits_24h);
    RUN_TEST(test_digits_12h_midnight_noon_and_afternoon);
    RUN_TEST(test_drift_stays_in_bounds_and_alternates_layout);
    RUN_TEST(test_drift_moves_and_swap_period_is_ten_minutes);
}
