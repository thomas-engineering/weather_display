#include "app_format.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Short weekday names, Sunday-first to match struct tm's tm_wday. */
static const char *const wday_short[LANG_COUNT][7] = {
    /* EN */ { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" },
    /* DE */ { "So",  "Mo",  "Di",  "Mi",  "Do",  "Fr",  "Sa"  },
    /* ES */ { "dom", "lun", "mar", "mié", "jue", "vie", "sáb" },
    /* FR */ { "dim", "lun", "mar", "mer", "jeu", "ven", "sam" },
};

static const char *const mon_short[LANG_COUNT][12] = {
    /* EN */ { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" },
    /* DE */ { "Jan", "Feb", "Mär", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez" },
    /* ES */ { "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic" },
    /* FR */ { "janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc." },
};

/* Not in the LVGL export's string tables — lifted from the design's getStrings().rf. */
typedef struct { const char *same, *cooler, *warmer, *rain, *breezy; } rf_strings_t;

static const rf_strings_t rf[LANG_COUNT] = {
    [LANG_EN] = {
        .same   = "Feels about the same as the air temperature.",
        .cooler = "Feels noticeably cooler than the air temperature, likely due to wind.",
        .warmer = "Feels warmer than the air temperature, likely due to humidity.",
        .rain   = " Rain is likely — plan accordingly.",
        .breezy = " Winds are breezy.",
    },
    [LANG_DE] = {
        .same   = "Fühlt sich etwa wie die Lufttemperatur an.",
        .cooler = "Fühlt sich durch den Wind deutlich kühler an.",
        .warmer = "Fühlt sich durch die Luftfeuchtigkeit wärmer an.",
        .rain   = " Regen ist wahrscheinlich — plane entsprechend.",
        .breezy = " Es ist windig.",
    },
    [LANG_ES] = {
        .same   = "Se siente similar a la temperatura del aire.",
        .cooler = "Se siente notablemente más frío por el viento.",
        .warmer = "Se siente más cálido por la humedad.",
        .rain   = " Es probable que llueva — actúa en consecuencia.",
        .breezy = " Hay viento.",
    },
    [LANG_FR] = {
        .same   = "Ressenti proche de la température de l'air.",
        .cooler = "Ressenti nettement plus frais à cause du vent.",
        .warmer = "Ressenti plus chaud à cause de l'humidité.",
        .rain   = " La pluie est probable — prévoyez en conséquence.",
        .breezy = " Il y a du vent.",
    },
};

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
        snprintf(out, n, "%s, %d. %s", wday_short[lang][wd], t->tm_mday, mon_short[lang][mo]);
    else if (lang == LANG_FR)
        snprintf(out, n, "%s %d %s", wday_short[lang][wd], t->tm_mday, mon_short[lang][mo]);
    else if (lang == LANG_ES)
        snprintf(out, n, "%s, %d %s", wday_short[lang][wd], t->tm_mday, mon_short[lang][mo]);
    else
        snprintf(out, n, "%s, %s %d", wday_short[lang][wd], mon_short[lang][mo], t->tm_mday);
}

void fmt_day_date(char *out, size_t n, const struct tm *t, weather_lang_t lang) {
    lang = clamp_lang(lang);
    int mo = (t->tm_mon >= 0 && t->tm_mon < 12) ? t->tm_mon : 0;
    if (lang == LANG_EN) snprintf(out, n, "%s %d", mon_short[lang][mo], t->tm_mday);
    else if (lang == LANG_DE) snprintf(out, n, "%d. %s", t->tm_mday, mon_short[lang][mo]);
    else snprintf(out, n, "%d %s", t->tm_mday, mon_short[lang][mo]);
}

void fmt_day_label(char *out, size_t n, const struct tm *t, int idx, weather_lang_t lang) {
    lang = clamp_lang(lang);
    if (idx == 0) { snprintf(out, n, "%s", weather_strings[lang].today); return; }
    if (idx == 1) { snprintf(out, n, "%s", weather_strings[lang].tomorrow); return; }
    int wd = (t->tm_wday >= 0 && t->tm_wday < 7) ? t->tm_wday : 0;
    snprintf(out, n, "%s", wday_short[lang][wd]);
}

void fmt_real_feel(char *out, size_t n, float apparent_c, float actual_c,
                   float wind_kmh, int precip_prob, weather_lang_t lang) {
    lang = clamp_lang(lang);
    float diff = apparent_c - actual_c;
    const char *base = (diff <= -3.0f) ? rf[lang].cooler
                     : (diff >=  3.0f) ? rf[lang].warmer
                                       : rf[lang].same;
    const char *tail = (precip_prob >= 50) ? rf[lang].rain
                     : (wind_kmh >= 30.0f) ? rf[lang].breezy
                                           : "";
    snprintf(out, n, "%s%s", base, tail);
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
