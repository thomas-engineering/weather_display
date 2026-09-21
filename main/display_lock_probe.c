#include "display_lock_probe.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

static const char *TAG = "display_lock";

/* Budget above which a single hold is worth a WARN — chosen well above one
 * LVGL frame period (the adapter targets ~20ms, see main.c's own
 * lvgl_stall_probe_cb) so routine work doesn't spam the log. */
#define HOLD_WARN_THRESHOLD_US (50 * 1000)

static int64_t s_locked_at_us;
static const char *s_site;
static int64_t s_max_hold_us;
static char s_max_hold_site[32];

bool display_lock_timed(int timeout_ms, const char *site) {
    bool ok = bsp_display_lock(timeout_ms);
    if (ok) {
        s_locked_at_us = esp_timer_get_time();
        s_site = site;
    }
    return ok;
}

void display_unlock_timed(void) {
    int64_t held_us = esp_timer_get_time() - s_locked_at_us;
    if (held_us > s_max_hold_us) {
        s_max_hold_us = held_us;
        snprintf(s_max_hold_site, sizeof s_max_hold_site, "%s", s_site ? s_site : "?");
    }
    if (held_us > HOLD_WARN_THRESHOLD_US) {
        ESP_LOGW(TAG, "display lock held %lldms at '%s' (worst seen: %lldms at '%s')",
                 (long long)(held_us / 1000), s_site ? s_site : "?",
                 (long long)(s_max_hold_us / 1000), s_max_hold_site);
    }
    bsp_display_unlock();
}
