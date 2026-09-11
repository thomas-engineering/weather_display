#include "app_format.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Weekday/month names and the "real feel" sentence fragments used below live
 * in weather_i18n.c (weather_wday_short / weather_mon_short / weather_real_feel)
 * alongside every other translated string, not duplicated here. */

static weather_lang_t clamp_lang(weather_lang_t l) { return (l < 0 || l >= LANG_COUNT) ? LANG_EN : l; }

void fmt_time(char *out, size_t n, const struct tm *t, wx_time_fmt_t fmt) {
    if (fmt == WX_TIME_12) {
        int h = t->tm_hour % 12;
        if (h == 0) h = 12;
        snprintf(out, n, "%d:%02d %s", h, t->tm_min, t->tm_hour < 12 ? "AM" : "PM");
    } else {
        snprintf(out, n, "%02d:%02d", t->tm_hour, t->tm_min);
    }
}

void fmt_date(char *out, size_t n, const struct tm *t, weather_lang_t lang) {
    lang = clamp_lang(lang);
    int wd = (t->tm_wday >= 0 && t->tm_wday < 7) ? t->tm_wday : 0;
    int mo = (t->tm_mon >= 0 && t->tm_mon < 12) ? t->tm_mon : 0;
    /* German and French put the day before the month; English and Spanish don't. */
    if (lang == LANG_DE)
        snprintf(out, n, "%s, %d. %s", weather_wday_short[lang][wd], t->tm_mday, weather_mon_short[lang][mo]);
    else if (lang == LANG_FR)
        snprintf(out, n, "%s %d %s", weather_wday_short[lang][wd], t->tm_mday, weather_mon_short[lang][mo]);
    else if (lang == LANG_ES)
        snprintf(out, n, "%s, %d %s", weather_wday_short[lang][wd], t->tm_mday, weather_mon_short[lang][mo]);
    else
        snprintf(out, n, "%s, %s %d", weather_wday_short[lang][wd], weather_mon_short[lang][mo], t->tm_mday);
}

void fmt_day_date(char *out, size_t n, const struct tm *t, weather_lang_t lang) {
    lang = clamp_lang(lang);
    int mo = (t->tm_mon >= 0 && t->tm_mon < 12) ? t->tm_mon : 0;
    if (lang == LANG_EN) snprintf(out, n, "%s %d", weather_mon_short[lang][mo], t->tm_mday);
    else if (lang == LANG_DE) snprintf(out, n, "%d. %s", t->tm_mday, weather_mon_short[lang][mo]);
    else snprintf(out, n, "%d %s", t->tm_mday, weather_mon_short[lang][mo]);
}

void fmt_day_label(char *out, size_t n, const struct tm *t, int idx, weather_lang_t lang) {
    lang = clamp_lang(lang);
    if (idx == 0) { snprintf(out, n, "%s", weather_strings[lang].today); return; }
    if (idx == 1) { snprintf(out, n, "%s", weather_strings[lang].tomorrow); return; }
    int wd = (t->tm_wday >= 0 && t->tm_wday < 7) ? t->tm_wday : 0;
    snprintf(out, n, "%s", weather_wday_short[lang][wd]);
}

void fmt_real_feel(char *out, size_t n, float apparent_c, float actual_c,
                   float wind_kmh, int precip_prob, weather_lang_t lang) {
    lang = clamp_lang(lang);
    float diff = apparent_c - actual_c;
    const char *base = (diff <= -3.0f) ? weather_real_feel[lang].cooler
                     : (diff >=  3.0f) ? weather_real_feel[lang].warmer
                                       : weather_real_feel[lang].same;
    const char *tail = (precip_prob >= 50) ? weather_real_feel[lang].rain
                     : (wind_kmh >= 30.0f) ? weather_real_feel[lang].breezy
                                           : "";
    snprintf(out, n, "%s%s", base, tail);
}

void fmt_iso_time(char *out, size_t n, const char *iso, wx_time_fmt_t fmt) {
    out[0] = '\0';
    if (!iso) return;
    const char *t = strchr(iso, 'T');
    if (!t || strlen(t + 1) < 5) return;
    int h, m;
    if (sscanf(t + 1, "%2d:%2d", &h, &m) != 2) return;
    struct tm tm0 = {0};
    tm0.tm_hour = h;
    tm0.tm_min = m;
    fmt_time(out, n, &tm0, fmt);
}

void fmt_time_ago(char *out, size_t n, time_t last_success, time_t now, weather_lang_t lang) {
    lang = clamp_lang(lang);
    out[0] = '\0';
    if (last_success == 0) return;
    long diff_sec = (long)(now - last_success);
    if (diff_sec < 0) diff_sec = 0;
    long diff_min = (diff_sec + 30) / 60; /* round to nearest minute */
    if (diff_min < 1) snprintf(out, n, "%s", weather_strings[lang].data_updated_now);
    else if (diff_min < 60) snprintf(out, n, weather_strings[lang].data_updated_ago_min, (int)diff_min);
    else snprintf(out, n, weather_strings[lang].data_updated_ago_hour, (int)((diff_min + 30) / 60));
}

const char *uv_category(bool has_value, float uv, weather_lang_t lang) {
    lang = clamp_lang(lang);
    if (!has_value) return "";
    if (uv < 3.0f) return weather_strings[lang].uv_low;
    if (uv < 6.0f) return weather_strings[lang].uv_moderate;
    if (uv < 8.0f) return weather_strings[lang].uv_high;
    if (uv < 11.0f) return weather_strings[lang].uv_very_high;
    return weather_strings[lang].uv_extreme;
}

const char *aqi_category(bool has_value, int aqi, weather_lang_t lang) {
    lang = clamp_lang(lang);
    if (!has_value) return "";
    if (aqi <= 50) return weather_strings[lang].aqi_good;
    if (aqi <= 100) return weather_strings[lang].aqi_moderate;
    if (aqi <= 150) return weather_strings[lang].aqi_unhealthy_sensitive;
    if (aqi <= 200) return weather_strings[lang].aqi_unhealthy;
    if (aqi <= 300) return weather_strings[lang].aqi_very_unhealthy;
    return weather_strings[lang].aqi_hazardous;
}

bool parse_iso_date(const char *s, struct tm *out) {
    if (!s || strlen(s) < 10) return false;
    int y, m, d;
    if (sscanf(s, "%4d-%2d-%2d", &y, &m, &d) != 3) return false;
    memset(out, 0, sizeof(*out));
    out->tm_year = y - 1900;
    out->tm_mon  = m - 1;
    out->tm_mday = d;
    out->tm_hour = 12;    /* midday, so a DST shift can't roll the date over */
    out->tm_isdst = -1;
    /* timegm-normalize to fill in tm_wday without dragging in the local timezone. */
    time_t tt = timegm(out);
    if (tt == (time_t)-1) return false;
    gmtime_r(&tt, out);
    return true;
}
