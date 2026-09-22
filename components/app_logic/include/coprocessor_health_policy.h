#ifndef COPROCESSOR_HEALTH_POLICY_H
#define COPROCESSOR_HEALTH_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/* Hardware-free escalation for "the co-processor itself has stopped
 * answering". No IDF headers, no driver/, no freertos/ — see CLAUDE.md's
 * "Codeorganisation". main/app_wifi.c feeds the events and carries out the
 * action.
 *
 * This is the third and last layer, and it covers what the other two cannot:
 *
 *   wifi_reconnect_policy  — the driver reports a problem and we retry.
 *   link_health_policy     — the driver reports nothing, but fetches fail,
 *                            so we ask the driver and then force a rejoin.
 *   this file              — the calls we would use to ask or to rejoin do
 *                            not come back either.
 *
 * Seen twice on hardware, 2026-09-22: `eh_host_feat_rpc: request: no
 * response` on every call, `esp_wifi_connect refused: ESP_FAIL` repeating
 * forever, every DNS lookup failing identically because nothing could reach
 * the co-processor at all. It never recovered on its own — both times a
 * human had to unplug the board. Everything link_health_policy escalates to
 * travels over the same wedged channel, so it escalates into the void.
 *
 * The distinguishing evidence is narrow and has to stay that way. During an
 * ordinary outage — the AP is off, out of range, rebooting — the RPC calls
 * themselves still *succeed*; it is the Wi-Fi association that fails, and it
 * fails through events, not through return codes. A call that returns a
 * transport-shaped error is therefore something else entirely, and only that
 * counts here. Getting this wrong in the permissive direction means
 * restarting the device during a router reboot, which is why the thresholds
 * below are deliberately slow: several failures in a row, sustained for
 * minutes, with a long cooldown, and a hard cap on how often recovery may be
 * attempted before the device stops trying and leaves the error on screen. */

typedef enum {
    CHP_EV_RPC_OK,      /* a call to the co-processor came back */
    CHP_EV_RPC_FAILED,  /* ...or failed in a way that looks like the transport */
    CHP_EV_LINK_UP,     /* got an address: the whole path demonstrably works */
    CHP_EV_TICK,        /* periodic, so a healthy stretch is noticed without traffic */
} coproc_health_event_t;

typedef enum {
    CHP_ACT_NONE,
    CHP_ACT_RECOVER,  /* reset the co-processor; on this board that means restarting
                       * the host, whose boot drives the co-processor's reset line */
    CHP_ACT_GIVE_UP,  /* recovered this often without it lasting — stop and report,
                       * reported once per episode so the caller can show it */
} coproc_health_action_t;

typedef struct {
    /* config */
    int     fail_threshold;  /* consecutive transport-shaped failures */
    int64_t persist_ms;      /* ...that have been going on at least this long */
    int64_t cooldown_ms;     /* minimum gap between two recoveries */
    int     max_recoveries;  /* give up after this many that did not last */
    int64_t healthy_ms;      /* time online that clears the recovery count */
    /* state */
    int     consecutive_failures;
    int64_t first_failure_ms;
    int64_t last_recovery_ms;
    int64_t online_since_ms;
    bool    online;
    bool    gave_up;
    int     recoveries;
} coproc_health_policy_t;

/* Slow on purpose — see the header comment. Five failures at app_wifi's 15s
 * retry cadence is over a minute of evidence before the two-minute persistence
 * window can even be met. */
#define CHP_FAIL_THRESHOLD   5
#define CHP_PERSIST_MS       (2 * 60 * 1000)
#define CHP_COOLDOWN_MS      (10 * 60 * 1000)
#define CHP_MAX_RECOVERIES   3
#define CHP_HEALTHY_MS       (10 * 60 * 1000)

/* `recoveries_so_far` is restored by the caller from non-volatile storage:
 * the recovery action is a restart, so the count has to survive it or the cap
 * means nothing. Non-positive config values fall back to the defaults. */
void coproc_health_policy_init(coproc_health_policy_t *p, int recoveries_so_far);

coproc_health_action_t coproc_health_policy_observe(coproc_health_policy_t *p,
                                                    coproc_health_event_t ev,
                                                    int64_t now_ms);

/* For persisting the count. Changes only as a result of observe(). */
int coproc_health_policy_recoveries(const coproc_health_policy_t *p);

#endif
