#include "screensaver_policy.h"

#include <math.h>

#define DRIFT_X_AMPLITUDE_PX 28
#define DRIFT_Y_AMPLITUDE_PX 14
#define DRIFT_X_PERIOD_MIN 37
#define DRIFT_Y_PERIOD_MIN 23
#define SWAP_PERIOD_S 600
#define SECONDS_PER_MINUTE 60
#define HOURS_PER_HALF_DAY 12
#define TWO_PI 6.28318530717958647692

const int k_saver_timeout_minutes[SAVER_TIMEOUT_OPTION_COUNT] = { 0, 15, 30, 60 };

bool saver_timeout_valid(int minutes) {
    for (int i = 0; i < SAVER_TIMEOUT_OPTION_COUNT; i++)
        if (k_saver_timeout_minutes[i] == minutes) return true;
    return false;
}

bool saver_style_valid(int style) { return style >= 0 && style < SAVER_STYLE_COUNT; }

void screensaver_policy_init(screensaver_policy_t *p) { p->active = false; }

saver_event_t screensaver_policy_tick(screensaver_policy_t *p, int timeout_minutes,
                                      uint32_t inactive_ms, uint32_t ms_per_unit) {
    if (p->active || timeout_minutes <= 0) return SAVER_EVENT_NONE;
    uint64_t threshold_ms = (uint64_t)timeout_minutes * ms_per_unit;
    if (inactive_ms < threshold_ms) return SAVER_EVENT_NONE;
    p->active = true;
    return SAVER_EVENT_START;
}

saver_event_t screensaver_policy_on_touch(screensaver_policy_t *p) {
    if (!p->active) return SAVER_EVENT_NONE;
    p->active = false;
    return SAVER_EVENT_STOP;
}

saver_event_t screensaver_policy_setting_changed(screensaver_policy_t *p, int timeout_minutes) {
    if (timeout_minutes > 0) return SAVER_EVENT_NONE;
    return screensaver_policy_on_touch(p);
}

void screensaver_split_digits(int hour, int minute, int second, bool use_12h,
                              uint8_t out[SAVER_DIGIT_COUNT]) {
    if (use_12h) {
        hour %= HOURS_PER_HALF_DAY;
        if (hour == 0) hour = HOURS_PER_HALF_DAY;
    }
    out[0] = (uint8_t)((hour / 10) % 10);
    out[1] = (uint8_t)(hour % 10);
    out[2] = (uint8_t)((minute / 10) % 10);
    out[3] = (uint8_t)(minute % 10);
    out[4] = (uint8_t)((second / 10) % 10);
    out[5] = (uint8_t)(second % 10);
}

saver_drift_t screensaver_drift(int64_t unix_seconds) {
    int64_t minute = unix_seconds / SECONDS_PER_MINUTE;
    saver_drift_t d;
    /* floor(x + 0.5) matches JavaScript's Math.round(), which the design uses. */
    d.dx = (int)floor(DRIFT_X_AMPLITUDE_PX * sin((double)(minute % DRIFT_X_PERIOD_MIN) * TWO_PI / DRIFT_X_PERIOD_MIN) + 0.5);
    d.dy = (int)floor(DRIFT_Y_AMPLITUDE_PX * sin((double)(minute % DRIFT_Y_PERIOD_MIN) * TWO_PI / DRIFT_Y_PERIOD_MIN) + 0.5);
    d.date_above = ((unix_seconds / SWAP_PERIOD_S) % 2) == 1;
    return d;
}
