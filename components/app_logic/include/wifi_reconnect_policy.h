#ifndef WIFI_RECONNECT_POLICY_H
#define WIFI_RECONNECT_POLICY_H

#include <stdbool.h>

/* Hardware-free Wi-Fi reconnect state machine. No IDF headers, no driver/,
 * no freertos/ — main/app_wifi.c keeps the esp_wifi_* and esp_timer side and
 * feeds every event through here, so "what should happen next" is decided in
 * code that runs on the host (./scripts/host-test.sh) instead of only being
 * verifiable by reflashing and waiting out a real outage.
 *
 * The one invariant this file exists to enforce:
 *
 *     whenever there is a network to rejoin and we are not on it, either a
 *     connect attempt is in flight, or the periodic retry timer is armed, or
 *     a scan is running that will decide on its own end.
 *
 * Every stall this replaces was a path that left none of the three running,
 * with the AP in range, until someone rebooted the device:
 *
 *   - esp_wifi_connect() refused the call (on this board it is a synchronous
 *     RPC to the ESP32-C6 and can fail on transport alone). No disconnect
 *     event follows a refused connect, and the old retry chain was driven
 *     purely by disconnect events, so the chain simply ended — see
 *     WRP_EV_CONNECT_REJECTED.
 *   - The DHCP lease went away while the station stayed associated. lwIP
 *     posts LOST_IP and no disconnect follows, so nothing noticed — see
 *     WRP_EV_LOST_IP.
 *   - A Wi-Fi scan suppressed reconnect handling wholesale for its whole
 *     duration and swallowed any disconnect that landed inside it — see
 *     WRP_EV_SCAN_BEGIN / WRP_EV_SCAN_END, which defer the retry instead.
 *
 * wifi_reconnect_policy_needs_recovery() and _is_recovering() state the
 * invariant directly; the host tests assert it after every single event. */

typedef enum {
    WRP_EV_STA_START,        /* the station interface came up */
    WRP_EV_CONNECT_REJECTED, /* the connect call we asked for returned an error */
    WRP_EV_DISCONNECTED,     /* association lost (or a connect attempt failed) */
    WRP_EV_GOT_IP,
    WRP_EV_LOST_IP,          /* address gone, association possibly still up */
    WRP_EV_SCAN_BEGIN,
    WRP_EV_SCAN_END,
    WRP_EV_MANUAL_CONNECT,   /* user/app asked for a specific network, right now */
    WRP_EV_FORGET,           /* stored network erased; nothing left to rejoin */
    WRP_EV_TIMER_TICK,       /* the periodic retry timer fired */
} wifi_reconnect_event_t;

/* What the caller must do as a result of the event it just reported. More
 * than one flag can be set by a single event, so this is a struct of flags
 * rather than a single enum. All-false means "do nothing". */
typedef struct {
    bool connect;       /* call esp_wifi_connect(), then report the outcome back:
                         * on an error, feed WRP_EV_CONNECT_REJECTED straight in */
    bool arm_timer;     /* start the periodic retry timer if it isn't running */
    bool disarm_timer;  /* stop it */
    bool signal_failed; /* the burst is spent / the connect was refused — release
                         * anyone blocked in a synchronous connect call */
    bool signal_up;     /* connected, with an address */
} wifi_reconnect_action_t;

typedef struct {
    bool started;            /* the station came up; before that there is nothing to recover */
    int  max_retries;        /* fast back-to-back burst budget, then the timer takes over */
    int  retries;
    bool have_credentials;
    bool want_reconnect;     /* false once the network is forgotten */
    bool scanning;
    bool retry_deferred;     /* a disconnect landed during a scan; act on it at scan end */
    bool connected;
    bool connect_in_flight;
    bool timer_armed;
    int  ignore_disconnects; /* self-inflicted disconnects to swallow (see _MANUAL_CONNECT) */
} wifi_reconnect_policy_t;

void wifi_reconnect_policy_init(wifi_reconnect_policy_t *p, int max_retries, bool have_credentials);

/* Feeds one event in and returns what the caller must do about it. */
wifi_reconnect_action_t wifi_reconnect_policy_event(wifi_reconnect_policy_t *p, wifi_reconnect_event_t ev);

/* True when the station is up, there is a network to rejoin, and we are not
 * on it. Also the right answer for the UI's "Reconnecting..." state: it
 * covers both the fast burst and the slower periodic retry, and is false both
 * when connected and when there is nothing to reconnect to. */
bool wifi_reconnect_policy_needs_recovery(const wifi_reconnect_policy_t *p);

/* True when something is actually scheduled to get us back. The invariant is
 * needs_recovery() implies is_recovering(); a false here with a true there is
 * exactly the stall this file was written to make impossible. */
bool wifi_reconnect_policy_is_recovering(const wifi_reconnect_policy_t *p);

#endif
