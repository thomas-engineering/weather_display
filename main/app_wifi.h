/* Wi-Fi station bring-up, scanning and runtime provisioning.
 *
 * The ESP32-P4 has no radio of its own — esp_wifi_remote forwards the esp_wifi API
 * over SDIO to the board's ESP32-C6 co-processor, so this looks like an ordinary
 * station driver but depends on esp_hosted being up first.
 *
 * Credentials entered on the device's own setup screen are stored in NVS and take
 * precedence over the Kconfig fallback, so a display can be re-homed to a new
 * network without a serial cable.
 */
#ifndef APP_WIFI_H
#define APP_WIFI_H

#include <stdbool.h>
#include <stddef.h>
#include "weather_ui.h"

#define APP_WIFI_MAX_SCAN 12

/* Initialises netif/event loop and the driver. Call once, before anything else. */
void app_wifi_init(void);

/* True if a network is stored in NVS or supplied by Kconfig. */
bool app_wifi_have_credentials(void);

/* Connects with the stored/fallback credentials. Blocks until connected or the
 * retry budget is spent. */
bool app_wifi_connect(void);

/* Connects with the given credentials and, on success, stores them in NVS.
 * Blocking; call from a worker task, never from the LVGL task. */
bool app_wifi_connect_with(const char *ssid, const char *password);

/* Erases the stored network from NVS and disconnects immediately, so the device
 * won't try to rejoin it after the next reboot. Call from a worker task, same as
 * app_wifi_connect_with(). */
void app_wifi_forget(void);

/* Blocking scan. Fills `out` with up to `max` networks, strongest first, and
 * returns how many were written. */
int app_wifi_scan(wx_wifi_network_t *out, int max);

/* True while the station holds an IP. */
bool app_wifi_is_connected(void);

/* True whenever there's a network to rejoin and the driver hasn't given up on
 * it yet — covers both the fast disconnect-triggered retry burst and the
 * slower periodic retry that follows once that burst is exhausted (see
 * app_wifi.c). False once connected, and also false with nothing to
 * reconnect to (no stored credentials, or the network was forgotten). Lets
 * the UI show a distinct "reconnecting" state during a longer outage instead
 * of just "offline". */
bool app_wifi_is_reconnecting(void);

/* Asks the driver whether the station is genuinely still associated, for the
 * case no Wi-Fi event covers: associated and addressed on paper, but every
 * request failing. Returns true if the association is real. If it isn't, our
 * own state is corrected and recovery starts, so the caller doesn't have to.
 * Cheap (one RPC); call it from a worker task, not the LVGL task. */
bool app_wifi_verify_link(void);

/* Tears the current association down and rejoins from scratch. For when
 * app_wifi_verify_link() said the link was fine and it still doesn't work.
 * Returns immediately — recovery runs on app_wifi's own worker task, so this
 * is safe to call from the weather worker without blocking it. */
void app_wifi_force_reconnect(void);

/* Fills ip/dns/gw (each must be >=16 bytes, IPSTR is "%d.%d.%d.%d") for the
 * Settings > Device information dialog. Returns false, buffers untouched, if
 * not connected or the netif has no address yet. */
bool app_wifi_get_ip_info(char *ip, size_t ip_len, char *dns, size_t dns_len, char *gw, size_t gw_len);

/* Starts SNTP and waits up to `timeout_ms` for the clock to be set. The header
 * clock and the "Today/Tomorrow" labels are wrong without this. Returns true
 * immediately once the clock has been set once. On a timeout SNTP keeps
 * retrying in the background; calling this again re-sends a request at once
 * instead of waiting out lwIP's own retry backoff. */
bool app_wifi_sync_time(int timeout_ms);

#endif
