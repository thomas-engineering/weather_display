#include "light_policy.h"

void light_policy_reset(light_policy_t *p) {
    p->ema_luma = -1;
    p->last_reported_pct = -1;
}

bool light_policy_sample(light_policy_t *p, uint8_t luma, int *out_pct) {
    /* Exponential smoothing: the OV5647's own auto-exposure already fights
     * ambient changes somewhat, so this is a coarse "did the room get
     * darker/brighter" signal, not a calibrated lux reading — smoothing keeps
     * momentary shadows and AEC settling from flickering the backlight. */
    p->ema_luma = (p->ema_luma < 0) ? luma : (p->ema_luma * 3 + luma) / 4;

    int pct = LIGHT_POLICY_BRIGHTNESS_MIN +
              (p->ema_luma * (LIGHT_POLICY_BRIGHTNESS_MAX - LIGHT_POLICY_BRIGHTNESS_MIN)) /
                  LIGHT_POLICY_LUMA_CEILING;
    if (pct < LIGHT_POLICY_BRIGHTNESS_MIN) pct = LIGHT_POLICY_BRIGHTNESS_MIN;
    if (pct > LIGHT_POLICY_BRIGHTNESS_MAX) pct = LIGHT_POLICY_BRIGHTNESS_MAX;

    bool should_report = p->last_reported_pct < 0 ||
                          (pct - p->last_reported_pct >= LIGHT_POLICY_CHANGE_THRESHOLD_PCT) ||
                          (p->last_reported_pct - pct >= LIGHT_POLICY_CHANGE_THRESHOLD_PCT);
    if (should_report) {
        p->last_reported_pct = pct;
        *out_pct = pct;
    }
    return should_report;
}
