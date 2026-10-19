#ifndef WEATHER_DISPLAY_MATH_H
#define WEATHER_DISPLAY_MATH_H

#include <stdbool.h>
#include <stddef.h>

/* Hardware-free helpers for the wind arrow, compass name and precipitation
 * text of the 2026-10-19 design sync ("extra data"). Same arithmetic as the
 * design's JavaScript (Weather App.dc.html: windArrowDeg, compass(),
 * precipStr()), kept here so it can be tested on the host. */

#define WDM_COMPASS_POINTS 8

/* Open-Meteo reports the direction the wind blows *from*; the arrow points
 * where it blows *to*: (deg + 180) mod 360, rounded to whole degrees. */
int wdm_arrow_deg(float wind_from_deg);

/* Index 0..7 into N, NE, E, SE, S, SW, W, NW (localized names live in
 * weather_i18n.c). Any finite input, negative or > 360, is normalized. */
int wdm_compass_index(float wind_from_deg);

/* "0.4 mm" below 10 mm, "12 mm" (rounded) from 10 mm up; negative or NaN
 * input shows as 0.0 mm. */
void wdm_format_precip_mm(char *out, size_t n, float mm);

/* Open-Meteo `is_day`: 0 = night. Anything else, including a missing field
 * (callers pass 1), is day. */
bool wdm_is_night(int is_day);

#endif
