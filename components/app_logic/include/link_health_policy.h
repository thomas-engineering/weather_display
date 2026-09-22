#ifndef LINK_HEALTH_POLICY_H
#define LINK_HEALTH_POLICY_H

#include <stdbool.h>

/* Hardware-free "the link says it is up but nothing works" escalation. No IDF
 * headers, no driver/, no freertos/ — see CLAUDE.md's "Codeorganisation".
 *
 * wifi_reconnect_policy.c handles everything the Wi-Fi driver actually tells
 * us about. This file covers the case where it tells us nothing at all: the
 * station stays associated and keeps an address, but every request fails —
 * a half-open association, a wedged SDIO/RPC path to the C6, an AP that
 * answers ARP and drops everything else. Nothing in the driver's event
 * stream reports that, so the only evidence is a run of failed fetches.
 *
 * main/app_weather.c feeds each fetch outcome in and carries out the action;
 * the thresholds live here so they can be tested without a board.
 *
 * Failures while Wi-Fi already knows it is offline are not evidence of
 * anything — wifi_reconnect_policy is already working on those — so they
 * reset the run instead of counting toward an escalation. */

typedef enum {
    LHP_ACT_NONE,
    LHP_ACT_VERIFY_LINK,     /* ask the driver whether we are really associated */
    LHP_ACT_FORCE_RECONNECT, /* checking didn't help: tear it down and rejoin */
} link_health_action_t;

typedef struct {
    int consecutive_failures;
    int escalations;      /* how many times we have forced a reconnect; diagnostics only */
    int verify_after;
    int reconnect_after;
} link_health_policy_t;

/* verify_after/reconnect_after are counts of consecutive failed fetches.
 * Non-positive values fall back to the defaults below. */
#define LINK_HEALTH_VERIFY_AFTER    3
#define LINK_HEALTH_RECONNECT_AFTER 5

void link_health_policy_init(link_health_policy_t *p, int verify_after, int reconnect_after);

/* Call once per fetch attempt. `wifi_claims_connected` is app_wifi's own view
 * (app_wifi_is_connected()) at the time of the attempt. Returns the action the
 * caller should take. After LHP_ACT_FORCE_RECONNECT the run resets, so a
 * reconnect is not re-forced on every subsequent failed fetch. */
link_health_action_t link_health_policy_on_fetch(link_health_policy_t *p, bool success,
                                                 bool wifi_claims_connected);

#endif
