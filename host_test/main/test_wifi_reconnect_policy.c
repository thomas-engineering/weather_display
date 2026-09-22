/*
 * Host unit tests for components/app_logic/wifi_reconnect_policy.
 *
 * These cover the "Wi-Fi stalls although the AP is right there" class of bug
 * that main/app_wifi.c could previously get into. Each of the first three
 * named tests is a regression test for one concrete stall found by review:
 *
 *   - a refused esp_wifi_connect() ending the retry chain, because the chain
 *     was driven purely by disconnect events and a refused connect produces
 *     none,
 *   - IP_EVENT_STA_LOST_IP going completely unnoticed, leaving the UI on
 *     "Online" while every request failed,
 *   - a Wi-Fi scan swallowing the disconnect that landed inside it.
 *
 * The last test is the important one: it drives a long pseudo-random event
 * sequence through the policy and asserts the invariant after every single
 * step, so a future edit that reintroduces *any* dead end fails here instead
 * of on a device three days later.
 */

#include "unity.h"
#include "wifi_reconnect_policy.h"

#define MAX_RETRY 8

/* Applies one event and asserts the invariant that the whole file exists for:
 * if we need to get back on a network, something must be scheduled to do it. */
static wifi_reconnect_action_t step(wifi_reconnect_policy_t *p, wifi_reconnect_event_t ev) {
    wifi_reconnect_action_t a = wifi_reconnect_policy_event(p, ev);
    if (wifi_reconnect_policy_needs_recovery(p)) {
        TEST_ASSERT_TRUE_MESSAGE(wifi_reconnect_policy_is_recovering(p),
                                 "off the network with nothing scheduled to get back on it");
    }
    return a;
}

/* Brings the policy to "connected and happy", the state most tests start from. */
static void bring_up(wifi_reconnect_policy_t *p) {
    wifi_reconnect_policy_init(p, MAX_RETRY, true);
    wifi_reconnect_action_t a = step(p, WRP_EV_STA_START);
    TEST_ASSERT_TRUE(a.connect);
    a = step(p, WRP_EV_GOT_IP);
    TEST_ASSERT_TRUE(a.signal_up);
    TEST_ASSERT_FALSE(wifi_reconnect_policy_needs_recovery(p));
}

static void test_refused_connect_falls_back_to_the_periodic_timer(void) {
    wifi_reconnect_policy_t p;
    bring_up(&p);

    /* The AP goes away and comes back, but the first reconnect call is
     * refused by the driver — on this board esp_wifi_connect() is an RPC to
     * the C6 and can fail on transport alone. */
    wifi_reconnect_action_t a = step(&p, WRP_EV_DISCONNECTED);
    TEST_ASSERT_TRUE(a.connect);

    a = step(&p, WRP_EV_CONNECT_REJECTED);

    /* This is the regression: before the fix nothing at all was scheduled
     * here and the device stayed offline until someone rebooted it. */
    TEST_ASSERT_TRUE(a.arm_timer);
    TEST_ASSERT_TRUE(p.timer_armed);
    TEST_ASSERT_TRUE(a.signal_failed);
    TEST_ASSERT_TRUE(wifi_reconnect_policy_is_recovering(&p));

    /* And the timer actually gets us back. */
    a = step(&p, WRP_EV_TIMER_TICK);
    TEST_ASSERT_TRUE(a.connect);
    a = step(&p, WRP_EV_GOT_IP);
    TEST_ASSERT_TRUE(a.disarm_timer);
    TEST_ASSERT_FALSE(p.timer_armed);
}

static void test_every_connect_being_refused_still_never_stalls(void) {
    wifi_reconnect_policy_t p;
    bring_up(&p);

    step(&p, WRP_EV_DISCONNECTED);
    /* Twenty ticks, every single connect refused. The invariant check inside
     * step() is the assertion here. */
    for (int i = 0; i < 20; i++) {
        step(&p, WRP_EV_CONNECT_REJECTED);
        wifi_reconnect_action_t a = step(&p, WRP_EV_TIMER_TICK);
        TEST_ASSERT_TRUE_MESSAGE(a.connect, "timer stopped trying");
    }
    TEST_ASSERT_TRUE(wifi_reconnect_policy_is_recovering(&p));
}

static void test_lost_ip_drives_recovery_on_its_own(void) {
    wifi_reconnect_policy_t p;
    bring_up(&p);

    /* DHCP lease expires, renewal doesn't land, association stays up. No
     * disconnect event will ever follow this one. */
    wifi_reconnect_action_t a = step(&p, WRP_EV_LOST_IP);

    TEST_ASSERT_TRUE_MESSAGE(a.connect, "LOST_IP did not trigger a reconnect");
    TEST_ASSERT_TRUE(wifi_reconnect_policy_needs_recovery(&p));
    TEST_ASSERT_FALSE_MESSAGE(p.connected, "still reporting Online without an address");

    a = step(&p, WRP_EV_GOT_IP);
    TEST_ASSERT_TRUE(a.signal_up);
    TEST_ASSERT_FALSE(wifi_reconnect_policy_needs_recovery(&p));
}

static void test_disconnect_during_a_scan_is_deferred_not_dropped(void) {
    wifi_reconnect_policy_t p;
    bring_up(&p);

    step(&p, WRP_EV_SCAN_BEGIN);
    /* The scan drops the association, as a scan does. */
    wifi_reconnect_action_t a = step(&p, WRP_EV_DISCONNECTED);
    TEST_ASSERT_FALSE_MESSAGE(a.connect, "reconnected in the middle of a scan");
    TEST_ASSERT_TRUE(p.retry_deferred);

    a = step(&p, WRP_EV_SCAN_END);
    TEST_ASSERT_TRUE_MESSAGE(a.connect, "the deferred retry was dropped at scan end");
}

static void test_scan_that_ends_while_offline_reconnects(void) {
    wifi_reconnect_policy_t p;
    wifi_reconnect_policy_init(&p, MAX_RETRY, true);
    step(&p, WRP_EV_STA_START);
    step(&p, WRP_EV_GOT_IP);

    /* No disconnect event inside the scan at all — but the scan still ended
     * with us off the network, which has to be repaired too. */
    step(&p, WRP_EV_SCAN_BEGIN);
    p.connected = false;
    p.connect_in_flight = false;
    wifi_reconnect_action_t a = step(&p, WRP_EV_SCAN_END);
    TEST_ASSERT_TRUE(a.connect);
}

static void test_manual_connect_does_not_burn_a_retry_on_its_own_disconnect(void) {
    wifi_reconnect_policy_t p;
    bring_up(&p);

    /* app_wifi.c's connect path disconnects first, then connects. */
    wifi_reconnect_action_t a = step(&p, WRP_EV_MANUAL_CONNECT);
    TEST_ASSERT_TRUE(a.connect);
    TEST_ASSERT_EQUAL_INT(0, p.retries);

    a = step(&p, WRP_EV_DISCONNECTED); /* the one we caused ourselves */
    TEST_ASSERT_FALSE_MESSAGE(a.connect, "self-inflicted disconnect fired a competing connect");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, p.retries, "self-inflicted disconnect spent a retry");
    TEST_ASSERT_TRUE(p.connect_in_flight);

    /* A genuine failure afterwards is still handled normally. */
    a = step(&p, WRP_EV_DISCONNECTED);
    TEST_ASSERT_TRUE(a.connect);
    TEST_ASSERT_EQUAL_INT(1, p.retries);
}

static void test_burst_exhausts_into_the_timer_then_recovers(void) {
    wifi_reconnect_policy_t p;
    bring_up(&p);

    wifi_reconnect_action_t a = step(&p, WRP_EV_DISCONNECTED);
    TEST_ASSERT_TRUE(a.connect);
    TEST_ASSERT_EQUAL_INT(1, p.retries);

    /* Spend the rest of the burst. */
    for (int i = 1; i < MAX_RETRY; i++) {
        a = step(&p, WRP_EV_DISCONNECTED);
        TEST_ASSERT_TRUE_MESSAGE(a.connect, "burst gave up early");
    }
    TEST_ASSERT_EQUAL_INT(MAX_RETRY, p.retries);

    /* One more failure and the fast burst hands over to the slow timer. */
    a = step(&p, WRP_EV_DISCONNECTED);
    TEST_ASSERT_FALSE(a.connect);
    TEST_ASSERT_TRUE(a.arm_timer);
    TEST_ASSERT_TRUE(a.signal_failed);

    /* The AP comes back an hour later; the timer is still trying. */
    a = step(&p, WRP_EV_TIMER_TICK);
    TEST_ASSERT_TRUE(a.connect);
    a = step(&p, WRP_EV_GOT_IP);
    TEST_ASSERT_TRUE(a.disarm_timer);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, p.retries, "burst budget was not restored after recovery");
}

static void test_timer_tick_does_not_stack_onto_an_in_flight_connect(void) {
    wifi_reconnect_policy_t p;
    bring_up(&p);

    step(&p, WRP_EV_DISCONNECTED); /* connect now in flight */
    p.timer_armed = true;          /* as it would be after an earlier outage */

    wifi_reconnect_action_t a = step(&p, WRP_EV_TIMER_TICK);
    TEST_ASSERT_FALSE_MESSAGE(a.connect, "timer fired a second connect over a live one");
}

static void test_forgotten_network_stops_trying(void) {
    wifi_reconnect_policy_t p;
    bring_up(&p);

    wifi_reconnect_action_t a = step(&p, WRP_EV_FORGET);
    TEST_ASSERT_TRUE(a.disarm_timer || !p.timer_armed);
    TEST_ASSERT_FALSE(wifi_reconnect_policy_needs_recovery(&p));

    /* Nothing reopens the chain by itself... */
    a = step(&p, WRP_EV_DISCONNECTED);
    TEST_ASSERT_FALSE(a.connect);
    a = step(&p, WRP_EV_TIMER_TICK);
    TEST_ASSERT_FALSE(a.connect);

    /* ...but entering a new network on the setup screen does. */
    a = step(&p, WRP_EV_MANUAL_CONNECT);
    TEST_ASSERT_TRUE(a.connect);
    TEST_ASSERT_TRUE(wifi_reconnect_policy_needs_recovery(&p));
}

/* Both of the following were found by the random sequence test below before
 * they were ever reachable in a hand-written case — kept as named tests so a
 * regression names itself instead of showing up as a fuzz failure. */

static void test_manual_connect_while_connected_drops_the_online_state(void) {
    wifi_reconnect_policy_t p;
    bring_up(&p);

    /* Switching networks from the setup screen while still associated to the
     * old one. The caller disconnects first, so we are off the network from
     * here — claiming otherwise made the next timer tick disarm itself. */
    step(&p, WRP_EV_MANUAL_CONNECT);
    TEST_ASSERT_FALSE_MESSAGE(p.connected, "still Online during a connect to another network");

    wifi_reconnect_action_t a = step(&p, WRP_EV_CONNECT_REJECTED);
    TEST_ASSERT_TRUE(a.arm_timer);

    a = step(&p, WRP_EV_TIMER_TICK);
    TEST_ASSERT_TRUE_MESSAGE(a.connect, "timer disarmed itself mid-outage");
    TEST_ASSERT_FALSE(a.disarm_timer);
}

static void test_self_inflicted_disconnect_after_a_refused_connect_still_recovers(void) {
    wifi_reconnect_policy_t p;
    bring_up(&p);

    /* The manual connect is refused outright, and only then does the
     * disconnect we caused ourselves arrive. There is no attempt left to
     * protect at that point, so swallowing it would strand the device. */
    step(&p, WRP_EV_MANUAL_CONNECT);
    step(&p, WRP_EV_CONNECT_REJECTED);

    wifi_reconnect_action_t a = step(&p, WRP_EV_DISCONNECTED);
    TEST_ASSERT_TRUE_MESSAGE(a.connect, "late self-inflicted disconnect was dropped");
    TEST_ASSERT_TRUE(wifi_reconnect_policy_is_recovering(&p));
}

/* Drives every event in pseudo-random order, modelling the adapter loop:
 * whenever the policy asks for a connect, the "driver" answers with one of
 * accepted-then-failed, accepted-then-succeeded, or refused outright. The
 * invariant assertion inside step() is what this test is for — no sequence of
 * events may leave the policy off a known network with nothing scheduled. */
static void test_invariant_holds_across_random_event_sequences(void) {
    static const wifi_reconnect_event_t events[] = {
        WRP_EV_STA_START, WRP_EV_DISCONNECTED, WRP_EV_GOT_IP, WRP_EV_LOST_IP,
        WRP_EV_SCAN_BEGIN, WRP_EV_SCAN_END, WRP_EV_MANUAL_CONNECT,
        WRP_EV_FORGET, WRP_EV_TIMER_TICK,
    };
    const int n_events = (int)(sizeof events / sizeof events[0]);

    for (unsigned seed = 1; seed <= 200; seed++) {
        wifi_reconnect_policy_t p;
        wifi_reconnect_policy_init(&p, MAX_RETRY, true);
        step(&p, WRP_EV_STA_START);

        unsigned rng = seed;
        for (int i = 0; i < 300; i++) {
            rng = rng * 1103515245u + 12345u;
            wifi_reconnect_action_t a = step(&p, events[(rng >> 16) % n_events]);

            /* Model the driver's answer to a connect we were told to issue. */
            while (a.connect) {
                rng = rng * 1103515245u + 12345u;
                switch ((rng >> 16) % 3) {
                case 0: a = step(&p, WRP_EV_CONNECT_REJECTED); break;
                case 1: a = step(&p, WRP_EV_DISCONNECTED); break;
                default: a = step(&p, WRP_EV_GOT_IP); break;
                }
            }
        }
    }
}

void test_wifi_reconnect_policy_run(void) {
    RUN_TEST(test_refused_connect_falls_back_to_the_periodic_timer);
    RUN_TEST(test_every_connect_being_refused_still_never_stalls);
    RUN_TEST(test_lost_ip_drives_recovery_on_its_own);
    RUN_TEST(test_disconnect_during_a_scan_is_deferred_not_dropped);
    RUN_TEST(test_scan_that_ends_while_offline_reconnects);
    RUN_TEST(test_manual_connect_does_not_burn_a_retry_on_its_own_disconnect);
    RUN_TEST(test_burst_exhausts_into_the_timer_then_recovers);
    RUN_TEST(test_timer_tick_does_not_stack_onto_an_in_flight_connect);
    RUN_TEST(test_forgotten_network_stops_trying);
    RUN_TEST(test_manual_connect_while_connected_drops_the_online_state);
    RUN_TEST(test_self_inflicted_disconnect_after_a_refused_connect_still_recovers);
    RUN_TEST(test_invariant_holds_across_random_event_sequences);
}
