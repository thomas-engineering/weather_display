#ifndef SAVE_DEBOUNCE_H
#define SAVE_DEBOUNCE_H

#include "esp_timer.h"

/* Coalesces rapid repeated save requests into a single deferred write, so
 * e.g. quickly tapping through several settings doesn't commit NVS once per
 * tap — generalizes the pattern app_prefs.c originally used only for
 * brightness_adaptive. The caller stores whatever it wants to save into its
 * own static "pending" state before calling this, then reads that same
 * state back inside cb when it eventually fires.
 *
 * *timer_slot is lazily created on first use (caller owns the static
 * storage — typically one static esp_timer_handle_t per save site) and
 * reused after that. Each call restarts the delay, so the write only
 * actually fires once activity has been quiet for delay_us. */
void save_debounce_fire(esp_timer_handle_t *timer_slot, const char *name,
                         esp_timer_cb_t cb, uint32_t delay_us);

#endif
