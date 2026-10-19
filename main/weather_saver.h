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

/* Chooses the colour style and registers the dismiss callback. Cheap: nothing is drawn or
 * allocated here. The overlay and its ~2 MB of pre-rendered images are built when the saver
 * is first shown (and rebuilt on the next show after a style change), so a device whose saver
 * is off never pays for them and Settings never stalls on a style switch. */
void weather_saver_set_style(saver_style_t style, weather_saver_dismiss_cb_t on_dismiss);

void weather_saver_show(void);
void weather_saver_hide(void);
bool weather_saver_is_visible(void);

/* Redraws changed digits and moves the clock/date by the burn-in drift.
 * `lt` is local wall-clock time, `unix_seconds` drives the drift. `date` is the
 * localized date line, `temp` the current temperature text (NULL/"" hides it). */
void weather_saver_update(const struct tm *lt, bool use_12h, int64_t unix_seconds,
                          const char *date, const char *temp);

#endif
