#include "weather_display_math.h"

#include <math.h>
#include <stdio.h>

#define DEG_FULL_CIRCLE 360.0f
#define DEG_HALF_CIRCLE 180.0f
#define DEG_PER_COMPASS_POINT 45.0f
#define PRECIP_ROUND_UP_MM 10.0f

/* JavaScript's Math.round(): halves round towards +infinity. */
static int round_js(float x) { return (int)floorf(x + 0.5f); }

static float wrap_deg(float deg) {
    float w = fmodf(deg, DEG_FULL_CIRCLE);
    return w < 0.0f ? w + DEG_FULL_CIRCLE : w;
}

int wdm_arrow_deg(float wind_from_deg) {
    if (!isfinite(wind_from_deg)) return 0;
    return round_js(fmodf(wrap_deg(wind_from_deg) + DEG_HALF_CIRCLE, DEG_FULL_CIRCLE));
}

int wdm_compass_index(float wind_from_deg) {
    if (!isfinite(wind_from_deg)) return 0;
    return round_js(wrap_deg(wind_from_deg) / DEG_PER_COMPASS_POINT) % WDM_COMPASS_POINTS;
}

void wdm_format_precip_mm(char *out, size_t n, float mm) {
    if (!isfinite(mm) || mm < 0.0f) mm = 0.0f;
    if (mm >= PRECIP_ROUND_UP_MM) snprintf(out, n, "%d mm", round_js(mm));
    else snprintf(out, n, "%.1f mm", (double)mm);
}

bool wdm_is_night(int is_day) { return is_day == 0; }
