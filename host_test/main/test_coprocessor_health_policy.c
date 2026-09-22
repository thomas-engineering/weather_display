/*
 * Host unit tests for components/app_logic/coprocessor_health_policy.
 *
 * The action this policy asks for is a restart, so the tests are mostly about
 * the cases where it must keep quiet: an ordinary outage, a short burst of
 * failures, a second recovery too soon after the first, and a device that has
 * already recovered as often as it is allowed to.
 */

#include "unity.h"
#include "coprocessor_health_policy.h"

#define MINUTE 60000LL

static void init(coproc_health_policy_t *p) {
    coproc_health_policy_init(p, 0);
}

/* Feeds `n` transport failures spaced `step_ms` apart starting at `t`, and
 * returns the first action that was not NONE. Feeding continues past it on
 * purpose: acting resets the run, so the caller also gets to see that the
 * failures immediately afterwards do not act again. */
static coproc_health_action_t fail_n(coproc_health_policy_t *p, int n,
                                     int64_t t, int64_t step_ms) {
    coproc_health_action_t first = CHP_ACT_NONE;
    for (int i = 0; i < n; i++) {
        coproc_health_action_t a =
            coproc_health_policy_observe(p, CHP_EV_RPC_FAILED, t + i * step_ms);
        if (a != CHP_ACT_NONE && first == CHP_ACT_NONE) first = a;
    }
    return first;
}

/* Walks the policy to a state where the only thing still missing is enough
 * evidence, past the boot cooldown. Returns the time reached. */
static int64_t past_cooldown(void) {
    return 20 * MINUTE;
}

static void test_an_ordinary_outage_never_recovers(void) {
    coproc_health_policy_t p;
    init(&p);
    /* A real outage produces no transport failures at all: the calls succeed,
     * the association is what breaks. Ticks and successful calls for an hour. */
    for (int64_t t = 0; t < 60 * MINUTE; t += MINUTE) {
        TEST_ASSERT_EQUAL_INT(CHP_ACT_NONE, coproc_health_policy_observe(&p, CHP_EV_TICK, t));
        TEST_ASSERT_EQUAL_INT(CHP_ACT_NONE, coproc_health_policy_observe(&p, CHP_EV_RPC_OK, t));
    }
}

static void test_a_short_burst_does_not_recover(void) {
    coproc_health_policy_t p;
    init(&p);
    int64_t t = past_cooldown();
    /* Enough failures, but all within a few seconds — not sustained. */
    TEST_ASSERT_EQUAL_INT(CHP_ACT_NONE, fail_n(&p, CHP_FAIL_THRESHOLD + 3, t, 1000));
}

static void test_a_long_run_of_failures_recovers(void) {
    coproc_health_policy_t p;
    init(&p);
    int64_t t = past_cooldown();
    /* The shape seen on hardware: a failure every 20s, going nowhere. */
    TEST_ASSERT_EQUAL_INT(CHP_ACT_RECOVER, fail_n(&p, 8, t, 20000));
    TEST_ASSERT_EQUAL_INT(1, coproc_health_policy_recoveries(&p));
}

static void test_no_recovery_during_the_first_minutes_of_uptime(void) {
    coproc_health_policy_t p;
    init(&p);
    /* Same evidence, but right after boot — a device that just started has not
     * been running long enough to conclude anything. */
    TEST_ASSERT_EQUAL_INT(CHP_ACT_NONE, fail_n(&p, 20, 0, 20000));
}

static void test_one_success_resets_the_run(void) {
    coproc_health_policy_t p;
    init(&p);
    int64_t t = past_cooldown();
    (void)fail_n(&p, CHP_FAIL_THRESHOLD + 2, t, 20000 /* not yet persisted */);
    coproc_health_policy_observe(&p, CHP_EV_RPC_OK, t + 3 * MINUTE);
    TEST_ASSERT_EQUAL_INT(CHP_ACT_NONE, fail_n(&p, CHP_FAIL_THRESHOLD - 1,
                                               t + 4 * MINUTE, 20000));
}

static void test_second_recovery_waits_for_the_cooldown(void) {
    coproc_health_policy_t p;
    init(&p);
    int64_t t = past_cooldown();
    TEST_ASSERT_EQUAL_INT(CHP_ACT_RECOVER, fail_n(&p, 8, t, 20000));

    /* Straight back into the same state — but too soon to act again. */
    int64_t t2 = t + 5 * MINUTE;
    TEST_ASSERT_EQUAL_INT(CHP_ACT_NONE, fail_n(&p, 10, t2, 20000));

    int64_t t3 = t + CHP_COOLDOWN_MS + MINUTE;
    TEST_ASSERT_EQUAL_INT(CHP_ACT_RECOVER, fail_n(&p, 8, t3, 20000));
}

static void test_gives_up_after_the_cap_and_says_so_once(void) {
    coproc_health_policy_t p;
    /* Restored from storage as if the two earlier recoveries had restarted it. */
    coproc_health_policy_init(&p, CHP_MAX_RECOVERIES);
    int64_t t = past_cooldown();
    TEST_ASSERT_EQUAL_INT(CHP_ACT_GIVE_UP, fail_n(&p, 8, t, 20000));
    /* Reported once — the caller shows an error, it must not repeat forever. */
    TEST_ASSERT_EQUAL_INT(CHP_ACT_NONE, fail_n(&p, 20, t + 3 * MINUTE, 20000));
    TEST_ASSERT_EQUAL_INT(CHP_MAX_RECOVERIES, coproc_health_policy_recoveries(&p));
}

static void test_a_healthy_stretch_clears_the_recovery_count(void) {
    coproc_health_policy_t p;
    coproc_health_policy_init(&p, 2);
    int64_t t = past_cooldown();
    coproc_health_policy_observe(&p, CHP_EV_LINK_UP, t);
    /* Still counted while the stretch is short... */
    coproc_health_policy_observe(&p, CHP_EV_TICK, t + MINUTE);
    TEST_ASSERT_EQUAL_INT(2, coproc_health_policy_recoveries(&p));
    /* ...cleared once it has lasted. */
    coproc_health_policy_observe(&p, CHP_EV_TICK, t + CHP_HEALTHY_MS + MINUTE);
    TEST_ASSERT_EQUAL_INT(0, coproc_health_policy_recoveries(&p));
}

static void test_a_failure_ends_the_healthy_stretch(void) {
    coproc_health_policy_t p;
    coproc_health_policy_init(&p, 2);
    int64_t t = past_cooldown();
    coproc_health_policy_observe(&p, CHP_EV_LINK_UP, t);
    coproc_health_policy_observe(&p, CHP_EV_RPC_FAILED, t + MINUTE);
    /* The clock must restart from the next LINK_UP, not carry on from before. */
    coproc_health_policy_observe(&p, CHP_EV_TICK, t + CHP_HEALTHY_MS + MINUTE);
    TEST_ASSERT_EQUAL_INT(2, coproc_health_policy_recoveries(&p));
}

static void test_null_policy_is_silent(void) {
    TEST_ASSERT_EQUAL_INT(CHP_ACT_NONE,
                          coproc_health_policy_observe(NULL, CHP_EV_RPC_FAILED, 0));
    TEST_ASSERT_EQUAL_INT(0, coproc_health_policy_recoveries(NULL));
}

void test_coprocessor_health_policy_run(void) {
    RUN_TEST(test_an_ordinary_outage_never_recovers);
    RUN_TEST(test_a_short_burst_does_not_recover);
    RUN_TEST(test_a_long_run_of_failures_recovers);
    RUN_TEST(test_no_recovery_during_the_first_minutes_of_uptime);
    RUN_TEST(test_one_success_resets_the_run);
    RUN_TEST(test_second_recovery_waits_for_the_cooldown);
    RUN_TEST(test_gives_up_after_the_cap_and_says_so_once);
    RUN_TEST(test_a_healthy_stretch_clears_the_recovery_count);
    RUN_TEST(test_a_failure_ends_the_healthy_stretch);
    RUN_TEST(test_null_policy_is_silent);
}
