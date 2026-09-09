#ifndef LIGHT_POLICY_H
#define LIGHT_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/* Hardware-free ambient-light -> backlight-percent policy. No IDF headers, no
 * driver/, no freertos/ — the camera and V4L2 side stays in main/app_light.c,
 * this is just the arithmetic, so it can run and be tested on the host
 * (./scripts/host-test.sh) instead of only being verifiable by reflashing. */

#define LIGHT_POLICY_BRIGHTNESS_MIN 10
#define LIGHT_POLICY_BRIGHTNESS_MAX 100

/* The OV5647's own AEC/AGC keeps the sensor's own exposure roughly correct for
 * whatever it's pointed at, which compresses average luma into a much
 * narrower band than the theoretical 0-255: on-hardware calibration (this
 * board, this lens) measured luma=1 covered, luma=60 in a dim room on an
 * overcast day, luma=117 pointed directly at a torch. Scaling against 255
 * would leave a merely bright room stuck around half brightness — scale
 * against this measured ceiling instead so typical bright-but-not-blinding
 * light reaches near BRIGHTNESS_MAX. Re-measure if the sensor, lens, or
 * mounting position changes. */
#define LIGHT_POLICY_LUMA_CEILING 130

/* Report a new brightness only once it moves by this much, so sensor/AEC
 * noise doesn't chatter the backlight PWM every sample. */
#define LIGHT_POLICY_CHANGE_THRESHOLD_PCT 3

typedef struct {
    int ema_luma;          /* -1 = no sample seen yet */
    int last_reported_pct; /* -1 = nothing reported yet */
} light_policy_t;

/* Also the right call whenever adaptive mode is turned off: the next time
 * it's turned back on, the first sample should be reported unconditionally
 * instead of being compared against a stale reading from before. */
void light_policy_reset(light_policy_t *p);

/* Feeds one new luma sample (0-255) through exponential smoothing and the
 * report-on-change threshold. Returns true and sets *out_pct when the caller
 * should apply a new brightness; returns false when the change was too small
 * to bother the backlight with (out_pct is left untouched). */
bool light_policy_sample(light_policy_t *p, uint8_t luma, int *out_pct);

#endif
