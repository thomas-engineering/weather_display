#ifndef SCREENSAVER_POLICY_H
#define SCREENSAVER_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/* Hardware-free screen saver policy: when it starts, when it stops, and the
 * pure arithmetic behind the Nixie clock (digit split, burn-in drift). The
 * LVGL drawing stays in main/weather_saver.c, the inactivity clock is LVGL's
 * own (lv_display_get_inactive_time()) and is passed in as a number, so all of
 * this runs on the host (./scripts/host-test.sh). */

#define SAVER_TIMEOUT_OPTION_COUNT 4
/* Minutes of inactivity before the saver starts; 0 = never. Index order is the
 * order of the Settings control. */
extern const int k_saver_timeout_minutes[SAVER_TIMEOUT_OPTION_COUNT];
#define SAVER_TIMEOUT_DEFAULT_IDX 0

typedef enum { SAVER_STYLE_THEME = 0, SAVER_STYLE_ORANGE, SAVER_STYLE_COUNT } saver_style_t;

#define SAVER_MS_PER_MINUTE 60000u
/* Test builds (CONFIG_WEATHER_SCREENSAVER_FAST_TEST) count the timeout setting in seconds. */
#define SAVER_MS_PER_SECOND 1000u

#define SAVER_DIGIT_COUNT 6 /* HH MM SS */

typedef enum { SAVER_EVENT_NONE, SAVER_EVENT_START, SAVER_EVENT_STOP } saver_event_t;

typedef struct {
    bool active;
} screensaver_policy_t;

bool saver_timeout_valid(int minutes);
bool saver_style_valid(int style);

void screensaver_policy_init(screensaver_policy_t *p);

/* Called about once a second. `inactive_ms` is the time since the last touch.
 * `ms_per_unit` is SAVER_MS_PER_MINUTE normally. Returns SAVER_EVENT_START
 * exactly once when the saver should appear; never while it is already active
 * or when the setting is off. */
saver_event_t screensaver_policy_tick(screensaver_policy_t *p, int timeout_minutes,
                                      uint32_t inactive_ms, uint32_t ms_per_unit);

/* A tap on the saver. Returns SAVER_EVENT_STOP when it was active. */
saver_event_t screensaver_policy_on_touch(screensaver_policy_t *p);

/* Turning the setting off while the saver is up (e.g. via a test hook) ends it. */
saver_event_t screensaver_policy_setting_changed(screensaver_policy_t *p, int timeout_minutes);

/* Splits a wall-clock time into six digits (HH MM SS). 12 h mode shows 01-12. */
void screensaver_split_digits(int hour, int minute, int second, bool use_12h,
                              uint8_t out[SAVER_DIGIT_COUNT]);

/* Slow drift plus a periodic swap of the clock/date order, so no pixel stays
 * lit at the same place for hours (same formula as the design). */
typedef struct {
    int dx, dy;
    bool date_above;
} saver_drift_t;
saver_drift_t screensaver_drift(int64_t unix_seconds);

#endif
