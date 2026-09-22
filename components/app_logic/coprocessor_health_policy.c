#include "coprocessor_health_policy.h"

void coproc_health_policy_init(coproc_health_policy_t *p, int recoveries_so_far) {
    if (!p) return;
    p->fail_threshold = CHP_FAIL_THRESHOLD;
    p->persist_ms = CHP_PERSIST_MS;
    p->cooldown_ms = CHP_COOLDOWN_MS;
    p->max_recoveries = CHP_MAX_RECOVERIES;
    p->healthy_ms = CHP_HEALTHY_MS;

    p->consecutive_failures = 0;
    p->first_failure_ms = 0;
    p->online_since_ms = 0;
    p->online = false;
    p->gave_up = false;
    p->recoveries = recoveries_so_far > 0 ? recoveries_so_far : 0;

    /* Timestamps are uptime, so leaving this at 0 measures the cooldown from
     * boot. That is what we want on both paths: a device that has just
     * restarted itself must not restart again straight away, and one that has
     * only been up a few minutes should not either — whatever it would be
     * recovering from, it has not been running long enough to know. */
    p->last_recovery_ms = 0;
}

int coproc_health_policy_recoveries(const coproc_health_policy_t *p) {
    return p ? p->recoveries : 0;
}

/* A stretch of working link clears the recovery count: whatever the problem
 * was, recovery dealt with it, and the next occurrence should get the full
 * budget again rather than inheriting a count from hours ago. */
static void clear_count_if_healthy(coproc_health_policy_t *p, int64_t now_ms) {
    if (p->online && p->recoveries > 0 &&
        (now_ms - p->online_since_ms) >= p->healthy_ms) {
        p->recoveries = 0;
        p->gave_up = false;
    }
}

static bool evidence_is_sufficient(const coproc_health_policy_t *p, int64_t now_ms) {
    return p->consecutive_failures >= p->fail_threshold &&
           (now_ms - p->first_failure_ms) >= p->persist_ms &&
           (now_ms - p->last_recovery_ms) >= p->cooldown_ms;
}

coproc_health_action_t coproc_health_policy_observe(coproc_health_policy_t *p,
                                                    coproc_health_event_t ev,
                                                    int64_t now_ms) {
    if (!p) return CHP_ACT_NONE;

    switch (ev) {
    case CHP_EV_LINK_UP:
        if (!p->online) {
            p->online = true;
            p->online_since_ms = now_ms;
        }
        p->consecutive_failures = 0;
        p->first_failure_ms = 0;
        clear_count_if_healthy(p, now_ms);
        return CHP_ACT_NONE;

    case CHP_EV_RPC_OK:
        /* The channel answered. That says nothing about the association, so
         * `online` is left alone — only an address proves the whole path. */
        p->consecutive_failures = 0;
        p->first_failure_ms = 0;
        clear_count_if_healthy(p, now_ms);
        return CHP_ACT_NONE;

    case CHP_EV_TICK:
        clear_count_if_healthy(p, now_ms);
        return CHP_ACT_NONE;

    case CHP_EV_RPC_FAILED:
        break;
    }

    p->online = false;
    if (p->consecutive_failures == 0) p->first_failure_ms = now_ms;
    p->consecutive_failures++;

    if (!evidence_is_sufficient(p, now_ms)) return CHP_ACT_NONE;

    if (p->recoveries >= p->max_recoveries) {
        /* Reported once. Restarting again would only produce another boot into
         * the same wedge, and a device stuck in a restart loop cannot even show
         * the user what is wrong. */
        if (p->gave_up) return CHP_ACT_NONE;
        p->gave_up = true;
        return CHP_ACT_GIVE_UP;
    }

    p->recoveries++;
    p->last_recovery_ms = now_ms;
    p->consecutive_failures = 0;
    p->first_failure_ms = 0;
    return CHP_ACT_RECOVER;
}
