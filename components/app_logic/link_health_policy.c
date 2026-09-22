#include "link_health_policy.h"

void link_health_policy_init(link_health_policy_t *p, int verify_after, int reconnect_after) {
    p->consecutive_failures = 0;
    p->escalations = 0;
    p->verify_after = verify_after > 0 ? verify_after : LINK_HEALTH_VERIFY_AFTER;
    p->reconnect_after = reconnect_after > 0 ? reconnect_after : LINK_HEALTH_RECONNECT_AFTER;
    /* A reconnect threshold at or below the verify threshold would skip the
     * cheap check entirely and go straight to tearing the association down. */
    if (p->reconnect_after <= p->verify_after) p->reconnect_after = p->verify_after + 1;
}

link_health_action_t link_health_policy_on_fetch(link_health_policy_t *p, bool success,
                                                 bool wifi_claims_connected) {
    if (success) {
        p->consecutive_failures = 0;
        return LHP_ACT_NONE;
    }

    if (!wifi_claims_connected) {
        /* A known outage explains this failure, and wifi_reconnect_policy is
         * already working on it. Forcing a reconnect on top would only cut
         * short a retry that is already in progress. */
        p->consecutive_failures = 0;
        return LHP_ACT_NONE;
    }

    p->consecutive_failures++;

    if (p->consecutive_failures >= p->reconnect_after) {
        /* Reset the run: the reconnect needs a fresh set of failures to prove
         * it didn't help, otherwise every later failed fetch would force
         * another one. */
        p->consecutive_failures = 0;
        p->escalations++;
        return LHP_ACT_FORCE_RECONNECT;
    }

    if (p->consecutive_failures == p->verify_after) return LHP_ACT_VERIFY_LINK;

    return LHP_ACT_NONE;
}
