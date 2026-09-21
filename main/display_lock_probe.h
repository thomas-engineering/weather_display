#ifndef DISPLAY_LOCK_PROBE_H
#define DISPLAY_LOCK_PROBE_H

#include <stdbool.h>

/* Thin wrapper around bsp_display_lock()/bsp_display_unlock() that measures
 * how long each hold actually took, tagged by call site. Existing
 * lvgl_stall_probe_cb() (main.c) can only report the gap between two of its
 * own ticks — it can't say whether that gap was the LVGL task waiting on
 * this lock (another task holding it), a slow widget callback, or genuine
 * flush/PPA cost (firmware-auditor Category G). A WARN logged from here at
 * the moment of release at least makes the lock-wait case separately
 * visible in the log, correlatable by timestamp against a lvgl_stall
 * warning.
 *
 * Not yet wired into every bsp_display_lock() call site in this project —
 * only the ones firmware-auditor Category G flagged as running an
 * unbounded cross-task call while holding the lock (push_device_info_to_ui's
 * callers in app_weather.c). Adopt it at any future call site doing
 * meaningfully slow work under the lock. */
bool display_lock_timed(int timeout_ms, const char *site);
void display_unlock_timed(void);

#endif
