/* Nixie-clock screen saver overlay (Claude Design, 2026-10-15 sync).
 *
 * Pure LVGL drawing: when it starts and stops is decided by
 * components/app_logic/screensaver_policy.c and driven from weather_ui.c. The
 * overlay lives on lv_layer_top(), so it stays above every dialog no matter
 * how weather_ui.c re-stacks them. */
#ifndef WEATHER_SAVER_H
#define WEATHER_SAVER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "lvgl.h"
#include "screensaver_policy.h"

/* Fired on a press anywhere on the overlay. */
typedef void (*weather_saver_dismiss_cb_t)(void);

/* Builds the overlay hidden. Calling it again rebuilds it in the new style
 * (keeping the visible/hidden state), which is how a style change is applied. */
void weather_saver_build(saver_style_t style, weather_saver_dismiss_cb_t on_dismiss);

void weather_saver_show(void);
void weather_saver_hide(void);
bool weather_saver_is_visible(void);

/* Redraws changed digits and moves the clock/date by the burn-in drift.
 * `lt` is local wall-clock time, `unix_seconds` drives the drift. `date` is the
 * localized date line, `temp` the current temperature text (NULL/"" hides it). */
void weather_saver_update(const struct tm *lt, bool use_12h, int64_t unix_seconds,
                          const char *date, const char *temp);

#endif
