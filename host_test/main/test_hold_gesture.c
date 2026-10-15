/* Host unit tests for components/app_logic/hold_gesture. */

#include "unity.h"
#include "hold_gesture.h"

#define HOLD_MS 3000

static void test_fires_once_after_the_hold_time(void) {
    hold_gesture_t h;
    hold_gesture_init(&h);
    hold_gesture_press(&h, 1000);
    TEST_ASSERT_FALSE(hold_gesture_poll(&h, 3999, HOLD_MS));
    TEST_ASSERT_TRUE(hold_gesture_poll(&h, 4000, HOLD_MS));
    TEST_ASSERT_FALSE(hold_gesture_poll(&h, 9000, HOLD_MS));
}

static void test_an_early_release_cancels(void) {
    hold_gesture_t h;
    hold_gesture_init(&h);
    hold_gesture_press(&h, 0);
    hold_gesture_release(&h);
    TEST_ASSERT_FALSE(hold_gesture_poll(&h, 10000, HOLD_MS));
}

static void test_a_new_press_starts_over(void) {
    hold_gesture_t h;
    hold_gesture_init(&h);
    hold_gesture_press(&h, 0);
    hold_gesture_release(&h);
    hold_gesture_press(&h, 2000);
    TEST_ASSERT_FALSE(hold_gesture_poll(&h, 4999, HOLD_MS));
    TEST_ASSERT_TRUE(hold_gesture_poll(&h, 5000, HOLD_MS));
}

static void test_fires_again_on_the_next_hold(void) {
    hold_gesture_t h;
    hold_gesture_init(&h);
    hold_gesture_press(&h, 0);
    TEST_ASSERT_TRUE(hold_gesture_poll(&h, HOLD_MS, HOLD_MS));
    hold_gesture_release(&h);
    hold_gesture_press(&h, 10000);
    TEST_ASSERT_TRUE(hold_gesture_poll(&h, 10000 + HOLD_MS, HOLD_MS));
}

static void test_never_fires_without_a_press(void) {
    hold_gesture_t h;
    hold_gesture_init(&h);
    TEST_ASSERT_FALSE(hold_gesture_poll(&h, 100000, HOLD_MS));
}

static void test_survives_the_millisecond_counter_wrapping(void) {
    hold_gesture_t h;
    hold_gesture_init(&h);
    hold_gesture_press(&h, UINT32_MAX - 1000);
    TEST_ASSERT_FALSE(hold_gesture_poll(&h, 1000, HOLD_MS)); /* 2001 ms later, wrapped */
    TEST_ASSERT_TRUE(hold_gesture_poll(&h, 2000, HOLD_MS));  /* 3001 ms later */
}

void test_hold_gesture_run(void) {
    RUN_TEST(test_fires_once_after_the_hold_time);
    RUN_TEST(test_an_early_release_cancels);
    RUN_TEST(test_a_new_press_starts_over);
    RUN_TEST(test_fires_again_on_the_next_hold);
    RUN_TEST(test_never_fires_without_a_press);
    RUN_TEST(test_survives_the_millisecond_counter_wrapping);
}
