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

/* Starts SNTP and waits up to `timeout_ms` for the clock to be set. The header
 * clock and the "Today/Tomorrow" labels are wrong without this. */
bool app_wifi_sync_time(int timeout_ms);

#endif
