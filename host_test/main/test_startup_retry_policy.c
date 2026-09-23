/*
 * Host unit tests for components/app_logic/startup_retry_policy.
 *
 * Covers the boot-time weather fetch and SNTP sync, which used to get
 * exactly one attempt each — see the sntp-no-retry-todo memory and
 * ota-c6-update-untested-todo's DNS findings from 2026-09-23.
 */

#include "unity.h"
#include "startup_retry_policy.h"

static void test_default_schedule_allows_three_retries(void) {
    startup_retry_policy_t p;
    startup_retry_policy_init(&p, 0); /* 0 -> default */
    TEST_ASSERT_EQUAL_INT(STARTUP_RETRY_MAX_ATTEMPTS, p.max_attempts);

    int delay_ms = -1;
    TEST_ASSERT_TRUE(startup_retry_policy_next(&p, &delay_ms));
    TEST_ASSERT_TRUE(delay_ms > 0);
    TEST_ASSERT_TRUE(startup_retry_policy_next(&p, &delay_ms));
    TEST_ASSERT_TRUE(startup_retry_policy_next(&p, &delay_ms));
    /* 4th attempt (max_attempts=4) is the last one allowed to run; no 5th retry. */
    TEST_ASSERT_FALSE(startup_retry_policy_next(&p, &delay_ms));
}

static void test_delays_increase(void) {
    startup_retry_policy_t p;
    startup_retry_policy_init(&p, 4);

    int first = -1, second = -1;
    TEST_ASSERT_TRUE(startup_retry_policy_next(&p, &first));
    TEST_ASSERT_TRUE(startup_retry_policy_next(&p, &second));
    TEST_ASSERT_TRUE_MESSAGE(second > first, "later retries should back off, not hammer the link");
}

static void test_custom_max_attempts_of_one_never_retries(void) {
    startup_retry_policy_t p;
    startup_retry_policy_init(&p, 1);

    int delay_ms = -1;
    TEST_ASSERT_FALSE(startup_retry_policy_next(&p, &delay_ms));
}

static void test_more_calls_than_the_delay_table_reuse_the_last_delay(void) {
    startup_retry_policy_t p;
    startup_retry_policy_init(&p, 100); /* far more attempts than the hand-tuned delay table has entries */

    int delay_ms = -1, last = -1;
    bool more = true;
    for (int i = 0; i < 20 && more; i++) {
        more = startup_retry_policy_next(&p, &delay_ms);
        if (more) {
            TEST_ASSERT_TRUE_MESSAGE(delay_ms >= last, "delay should never shrink once the table is exhausted");
            last = delay_ms;
        }
    }
}

void test_startup_retry_policy_run(void) {
    RUN_TEST(test_default_schedule_allows_three_retries);
    RUN_TEST(test_delays_increase);
    RUN_TEST(test_custom_max_attempts_of_one_never_retries);
    RUN_TEST(test_more_calls_than_the_delay_table_reuse_the_last_delay);
}
