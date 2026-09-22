/*
 * Host unit tests for components/app_logic/link_health_policy.
 *
 * Covers the stall that no Wi-Fi event ever reports: the station stays
 * associated and keeps its address, but every fetch fails. Before this
 * policy existed, main/app_weather.c treated each failure identically —
 * show the error bar, return — so the device could sit in "Online, nothing
 * works" indefinitely with no recovery action of any kind.
 */

#include "unity.h"
#include "link_health_policy.h"

#define VERIFY_AFTER    3
#define RECONNECT_AFTER 5

static void init(link_health_policy_t *p) {
    link_health_policy_init(p, VERIFY_AFTER, RECONNECT_AFTER);
}

/* Feeds `n` failed fetches with Wi-Fi claiming to be connected, asserting
 * that none of them escalates, and returns nothing. Used to walk up to just
 * below a threshold. */
static void fail_quietly(link_health_policy_t *p, int n) {
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(LHP_ACT_NONE, link_health_policy_on_fetch(p, false, true),
                                      "escalated earlier than the configured threshold");
    }
}

static void test_success_never_escalates(void) {
    link_health_policy_t p;
    init(&p);
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_EQUAL_INT(LHP_ACT_NONE, link_health_policy_on_fetch(&p, true, true));
    }
}

static void test_run_of_failures_verifies_then_forces_a_reconnect(void) {
    link_health_policy_t p;
    init(&p);

    fail_quietly(&p, VERIFY_AFTER - 1);

    /* Third failure in a row: cheap check first — are we actually associated? */
    TEST_ASSERT_EQUAL_INT(LHP_ACT_VERIFY_LINK, link_health_policy_on_fetch(&p, false, true));

    fail_quietly(&p, RECONNECT_AFTER - VERIFY_AFTER - 1);

    /* Checking didn't fix it and it is still failing: tear it down and rejoin. */
    TEST_ASSERT_EQUAL_INT(LHP_ACT_FORCE_RECONNECT, link_health_policy_on_fetch(&p, false, true));
    TEST_ASSERT_EQUAL_INT(1, p.escalations);
}

static void test_one_success_clears_the_run(void) {
    link_health_policy_t p;
    init(&p);

    fail_quietly(&p, VERIFY_AFTER - 1);
    TEST_ASSERT_EQUAL_INT(LHP_ACT_NONE, link_health_policy_on_fetch(&p, true, true));

    /* The counter restarted, so the next failures must not reach the verify
     * threshold early. */
    fail_quietly(&p, VERIFY_AFTER - 1);
    TEST_ASSERT_EQUAL_INT(LHP_ACT_VERIFY_LINK, link_health_policy_on_fetch(&p, false, true));
}

static void test_failures_while_offline_do_not_escalate(void) {
    link_health_policy_t p;
    init(&p);

    /* A known outage: wifi_reconnect_policy already owns this case, and
     * forcing a reconnect here would cut short a retry already in progress. */
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(LHP_ACT_NONE, link_health_policy_on_fetch(&p, false, false),
                                      "escalated during a known outage");
    }
    TEST_ASSERT_EQUAL_INT(0, p.escalations);
}

static void test_offline_failure_resets_an_online_run(void) {
    link_health_policy_t p;
    init(&p);

    fail_quietly(&p, VERIFY_AFTER - 1);
    /* Wi-Fi notices it is offline: the earlier failures are explained now. */
    TEST_ASSERT_EQUAL_INT(LHP_ACT_NONE, link_health_policy_on_fetch(&p, false, false));

    fail_quietly(&p, VERIFY_AFTER - 1);
    TEST_ASSERT_EQUAL_INT(LHP_ACT_VERIFY_LINK, link_health_policy_on_fetch(&p, false, true));
}

static void test_reconnect_is_not_forced_on_every_later_failure(void) {
    link_health_policy_t p;
    init(&p);

    /* First escalation. */
    for (int i = 0; i < RECONNECT_AFTER - 1; i++) link_health_policy_on_fetch(&p, false, true);
    TEST_ASSERT_EQUAL_INT(LHP_ACT_FORCE_RECONNECT, link_health_policy_on_fetch(&p, false, true));

    /* The very next failure must not force another one — the reconnect needs
     * a full fresh run of failures to prove it didn't help. */
    TEST_ASSERT_EQUAL_INT(LHP_ACT_NONE, link_health_policy_on_fetch(&p, false, true));
    TEST_ASSERT_EQUAL_INT(1, p.escalations);

    /* ...and a second escalation still eventually happens if it keeps failing. */
    for (int i = 0; i < RECONNECT_AFTER - 2; i++) link_health_policy_on_fetch(&p, false, true);
    TEST_ASSERT_EQUAL_INT(LHP_ACT_FORCE_RECONNECT, link_health_policy_on_fetch(&p, false, true));
    TEST_ASSERT_EQUAL_INT(2, p.escalations);
}

static void test_degenerate_thresholds_still_check_before_reconnecting(void) {
    link_health_policy_t p;
    /* Misconfigured so that reconnect would trigger at or before verify. */
    link_health_policy_init(&p, 3, 2);
    TEST_ASSERT_TRUE_MESSAGE(p.reconnect_after > p.verify_after,
                             "would tear down the association without checking first");

    link_health_policy_init(&p, 0, 0);
    TEST_ASSERT_EQUAL_INT(LINK_HEALTH_VERIFY_AFTER, p.verify_after);
    TEST_ASSERT_EQUAL_INT(LINK_HEALTH_RECONNECT_AFTER, p.reconnect_after);
}

void test_link_health_policy_run(void) {
    RUN_TEST(test_success_never_escalates);
    RUN_TEST(test_run_of_failures_verifies_then_forces_a_reconnect);
    RUN_TEST(test_one_success_clears_the_run);
    RUN_TEST(test_failures_while_offline_do_not_escalate);
    RUN_TEST(test_offline_failure_resets_an_online_run);
    RUN_TEST(test_reconnect_is_not_forced_on_every_later_failure);
    RUN_TEST(test_degenerate_thresholds_still_check_before_reconnecting);
}
