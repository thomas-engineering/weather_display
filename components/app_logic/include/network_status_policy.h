#ifndef NETWORK_STATUS_POLICY_H
#define NETWORK_STATUS_POLICY_H

#include <stdbool.h>

/* Hardware-free decision logic for the header error bar and the refresh
 * toast (main/weather_ui.c), driven by main/app_weather.c's fetch outcomes.
 * No IDF headers, no LVGL, no driver/ — see CLAUDE.md's "Codeorganisation".
 *
 * Claude Design's refresh() handler only ever sets the toast from a manual
 * refresh (the header icon or the error bar's own retry button); a silent
 * background refresh (periodic auto-refresh, city change, post-Wi-Fi-connect)
 * never shows it. That asymmetry is exactly what let a stale error toast from
 * an earlier failed manual retry outlive its own error: once a later, silent
 * refresh succeeded, nothing ever told the toast to go away again. This
 * policy makes that resolution explicit instead of leaving main/ to remember
 * it at every call site. */

typedef enum {
    NSP_TOAST_HIDDEN,
    NSP_TOAST_SUCCESS,
    NSP_TOAST_ERROR,
} nsp_toast_t;

typedef struct {
    bool        error_active; /* header error bar should be shown */
    nsp_toast_t toast;        /* refresh toast state to apply */
} network_status_policy_t;

void network_status_policy_reset(network_status_policy_t *p);

/* Call once per fetch attempt outcome. `is_manual` mirrors app_weather.c's
 * do_refresh_body(is_manual) flag. Updates p->error_active and p->toast in
 * place; the caller applies both to the UI (weather_ui_set_error(),
 * weather_ui_show_refresh_toast() / weather_ui_hide_refresh_toast()). */
void network_status_policy_on_fetch(network_status_policy_t *p, bool success, bool is_manual);

#endif
