/* Locale-aware date/time formatting and the "real feel" sentence.
 *
 * The LVGL port takes pre-formatted, pre-localized strings (see README_LVGL.md in
 * the design project) because LVGL ships no locale data. This module is the small
 * helper that mirrors the web version's Intl.DateTimeFormat / realFeelText output.
 */
#ifndef APP_FORMAT_H
#define APP_FORMAT_H

#include <time.h>
#include <stdbool.h>
#include "weather_i18n.h"
#include "weather_ui.h"

/* "14:05" or "2:05 PM" */
void fmt_time(char *out, size_t n, const struct tm *t, wx_time_fmt_t fmt);

/* "Thu, Sep 3" — weekday short, month short, day numeric. */
void fmt_date(char *out, size_t n, const struct tm *t, weather_lang_t lang);

/* "Sep 3" — month short, day numeric. */
void fmt_day_date(char *out, size_t n, const struct tm *t, weather_lang_t lang);

/* "Today" / "Tomorrow" for idx 0/1, otherwise the short weekday name. */
void fmt_day_label(char *out, size_t n, const struct tm *t, int idx, weather_lang_t lang);

/* Mirrors the web version's realFeelText(): a base sentence chosen from the
 * apparent-vs-actual delta, with an optional rain or wind clause appended. */
void fmt_real_feel(char *out, size_t n, float apparent_c, float actual_c,
                   float wind_kmh, int precip_prob, weather_lang_t lang);

/* Parses an Open-Meteo "YYYY-MM-DD" date into a struct tm (fields normalized so
 * tm_wday is valid). Returns false on a malformed string. */
bool parse_iso_date(const char *s, struct tm *out);

/* "06:45" or "6:45 AM" — reads the "HH:MM" after the 'T' in an Open-Meteo
 * local-time ISO string (e.g. "2026-09-12T06:45", as returned by the
 * `sunrise`/`sunset` daily fields). Writes "" if `iso` is NULL/malformed. */
void fmt_iso_time(char *out, size_t n, const char *iso, wx_time_fmt_t fmt);

/* Header "last synced" text (2026-09-12 sync), mirroring the design's
 * timeAgo(): "Updated just now" / "Updated N min ago" / "Updated N h ago".
 * `last_success` == 0 (never fetched) writes "". */
void fmt_time_ago(char *out, size_t n, time_t last_success, time_t now, weather_lang_t lang);

/* UV-index / US AQI value to a localized category string, same thresholds as
 * the design's uvCategory()/aqiCategory(). `has_value` false (no reading yet)
 * returns "". */
const char *uv_category(bool has_value, float uv, weather_lang_t lang);
const char *aqi_category(bool has_value, int aqi, weather_lang_t lang);

#endif
