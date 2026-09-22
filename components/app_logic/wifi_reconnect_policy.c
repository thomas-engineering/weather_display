#include "wifi_reconnect_policy.h"

#include <string.h>

void wifi_reconnect_policy_init(wifi_reconnect_policy_t *p, int max_retries, bool have_credentials) {
    memset(p, 0, sizeof *p);
    p->max_retries = max_retries > 0 ? max_retries : 1;
    p->have_credentials = have_credentials;
    p->want_reconnect = true;
}

static wifi_reconnect_action_t action_none(void) {
    wifi_reconnect_action_t a;
    memset(&a, 0, sizeof a);
    return a;
}

/* A fresh attempt that doesn't spend from the burst budget — the start of a
 * burst, not a step inside one. */
static void issue_connect(wifi_reconnect_policy_t *p, wifi_reconnect_action_t *a) {
    p->connect_in_flight = true;
    a->connect = true;
}

static void arm_timer(wifi_reconnect_policy_t *p, wifi_reconnect_action_t *a) {
    p->timer_armed = true;
    a->arm_timer = true;
}

static void disarm_timer(wifi_reconnect_policy_t *p, wifi_reconnect_action_t *a) {
    if (!p->timer_armed) return;
    p->timer_armed = false;
    a->disarm_timer = true;
}

/* Shared tail for every "we are off the network" event: spend one more
 * attempt from the fast burst, or fall back to the periodic timer once that
 * budget is gone — but never neither, which is the whole point of this file.
 * A scan in progress defers the decision to WRP_EV_SCAN_END rather than
 * dropping it. */
static void retry_or_arm(wifi_reconnect_policy_t *p, wifi_reconnect_action_t *a) {
    if (!p->want_reconnect || !p->have_credentials) return;
    if (p->scanning) {
        p->retry_deferred = true;
        return;
    }
    if (p->retries < p->max_retries) {
        p->retries++;
        issue_connect(p, a);
    } else {
        arm_timer(p, a);
        a->signal_failed = true;
    }
}

wifi_reconnect_action_t wifi_reconnect_policy_event(wifi_reconnect_policy_t *p, wifi_reconnect_event_t ev) {
    wifi_reconnect_action_t a = action_none();

    switch (ev) {
    case WRP_EV_STA_START:
        p->started = true;
        if (p->want_reconnect && p->have_credentials && !p->scanning) {
            p->retries = 0;
            issue_connect(p, &a);
        }
        break;

    case WRP_EV_MANUAL_CONNECT:
        /* An explicit connect to a named network: it always wins, including
         * over a forgotten-network state and over an in-progress scan (the
         * caller serializes those two against each other anyway).
         *
         * ignore_disconnects covers the disconnect the caller's own
         * esp_wifi_disconnect() is about to produce. Without it, that
         * self-inflicted event walks straight into retry_or_arm() and fires a
         * second, competing connect that races — and on real silicon can
         * abort — the deliberate one. */
        p->started = true;
        p->have_credentials = true;
        p->want_reconnect = true;
        /* The caller disconnects before it connects, so we are off the
         * network from this moment even if we were associated a line ago.
         * Leaving this true let a later timer tick conclude all was well and
         * disarm itself, in the middle of a connect that had already failed. */
        p->connected = false;
        p->retries = 0;
        p->retry_deferred = false;
        p->ignore_disconnects = 1;
        disarm_timer(p, &a);
        issue_connect(p, &a);
        break;

    case WRP_EV_DISCONNECTED:
        p->connected = false;
        if (p->ignore_disconnects > 0 && p->connect_in_flight) {
            /* Our own doing, and the deliberate attempt it precedes is still
             * in flight — swallowing it is what stops that attempt being
             * raced by a competing one. With nothing in flight there is
             * nothing to protect and dropping the event would strand us, so
             * it falls through and is handled like any other disconnect. */
            p->ignore_disconnects--;
            break;
        }
        p->ignore_disconnects = 0;
        p->connect_in_flight = false;
        retry_or_arm(p, &a);
        break;

    case WRP_EV_CONNECT_REJECTED:
        /* The driver refused the call, so no disconnect event will follow it
         * and the burst cannot carry itself forward. Hand straight over to the
         * periodic timer instead of retrying into the same refusal. */
        p->connect_in_flight = false;
        if (p->want_reconnect && p->have_credentials) {
            arm_timer(p, &a);
            a.signal_failed = true;
        }
        break;

    case WRP_EV_GOT_IP:
        p->connected = true;
        p->connect_in_flight = false;
        p->retries = 0;
        p->retry_deferred = false;
        p->ignore_disconnects = 0;
        disarm_timer(p, &a);
        a.signal_up = true;
        break;

    case WRP_EV_LOST_IP:
        /* Still associated as far as the driver is concerned, but the address
         * is gone (lease expiry, DHCP server restart). Nothing else reports
         * this, so it has to drive recovery itself. We were healthy a moment
         * ago, so the attempt gets a full burst budget rather than whatever
         * was left over from an older outage. */
        p->connected = false;
        p->connect_in_flight = false;
        p->retries = 0;
        retry_or_arm(p, &a);
        break;

    case WRP_EV_SCAN_BEGIN:
        p->scanning = true;
        break;

    case WRP_EV_SCAN_END:
        p->scanning = false;
        if (p->retry_deferred || (!p->connected && !p->connect_in_flight)) {
            p->retry_deferred = false;
            retry_or_arm(p, &a);
        }
        break;

    case WRP_EV_TIMER_TICK:
        if (p->connected || !p->want_reconnect || !p->have_credentials) {
            disarm_timer(p, &a);
        } else if (!p->scanning && !p->connect_in_flight) {
            /* Skipping the tick while an attempt is already in flight keeps
             * the timer from stacking a second connect on top of the burst. */
            issue_connect(p, &a);
        }
        break;

    case WRP_EV_FORGET:
        p->have_credentials = false;
        p->want_reconnect = false;
        p->connected = false;
        p->connect_in_flight = false;
        p->retry_deferred = false;
        p->ignore_disconnects = 0;
        disarm_timer(p, &a);
        break;
    }

    return a;
}

bool wifi_reconnect_policy_needs_recovery(const wifi_reconnect_policy_t *p) {
    return p->started && !p->connected && p->want_reconnect && p->have_credentials;
}

bool wifi_reconnect_policy_is_recovering(const wifi_reconnect_policy_t *p) {
    /* A scan counts: it always ends, and its end re-runs the decision. */
    return p->connect_in_flight || p->timer_armed || p->scanning;
}
