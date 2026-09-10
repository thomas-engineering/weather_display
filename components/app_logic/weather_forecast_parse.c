#include "weather_forecast_parse.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float jnum(const cJSON *o, const char *k, float dflt) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : dflt;
}

/* Reads element `i` of a numeric array, or `dflt` if absent/not a number. */
static float jarr(const cJSON *o, const char *k, int i, float dflt) {
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsArray(a)) return dflt;
    const cJSON *v = cJSON_GetArrayItem(a, i);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : dflt;
}

bool weather_forecast_parse(const char *json, wfp_forecast_t *out) {
    if (!json || !out) return false;

    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    if (!cJSON_IsObject(cur) || !cJSON_IsObject(daily)) {
        cJSON_Delete(root);
        return false;
    }

    wfp_forecast_t r = {0};

    r.utc_offset_sec = (int)jnum(root, "utc_offset_seconds", 0);
    r.temp_c         = jnum(cur, "temperature_2m", 0);
    r.humidity_pct   = (int)jnum(cur, "relative_humidity_2m", 0);
    r.feels_like_c   = jnum(cur, "apparent_temperature", r.temp_c);
    r.precip_pct     = (int)jnum(cur, "precipitation_probability", 0);
    r.wind_kmh       = jnum(cur, "wind_speed_10m", 0);
    r.weather_code   = (int)jnum(cur, "weather_code", 0);

    const cJSON *times = cJSON_GetObjectItemCaseSensitive(daily, "time");
    int n = cJSON_IsArray(times) ? cJSON_GetArraySize(times) : 0;
    if (n > WFP_DAYS) n = WFP_DAYS;
    r.day_count = n;
    for (int i = 0; i < n; i++) {
        const cJSON *t = cJSON_GetArrayItem(times, i);
        snprintf(r.days[i].iso_date, sizeof r.days[i].iso_date, "%s",
                 (cJSON_IsString(t) && t->valuestring) ? t->valuestring : "");
        r.days[i].weather_code = (int)jarr(daily, "weather_code", i, 0);
        r.days[i].temp_max_c   = jarr(daily, "temperature_2m_max", i, 0);
        r.days[i].temp_min_c   = jarr(daily, "temperature_2m_min", i, 0);
        r.days[i].feels_max_c  = jarr(daily, "apparent_temperature_max", i, r.days[i].temp_max_c);
        r.days[i].feels_min_c  = jarr(daily, "apparent_temperature_min", i, r.days[i].temp_min_c);
        r.days[i].precip_pct   = (int)jarr(daily, "precipitation_probability_max", i, 0);
        r.days[i].wind_max_kmh = jarr(daily, "wind_speed_10m_max", i, 0);
    }

    /* Hourly arrives as one flat 7x24 series; slice it per day. */
    const cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    if (cJSON_IsObject(hourly)) {
        const cJSON *ht = cJSON_GetObjectItemCaseSensitive(hourly, "time");
        int hn = cJSON_IsArray(ht) ? cJSON_GetArraySize(ht) : 0;
        for (int d = 0; d < n; d++) {
            int base = d * 24;
            if (base >= hn) break;
            int cnt = hn - base;
            if (cnt > WFP_HOURLY_POINTS) cnt = WFP_HOURLY_POINTS;
            r.hourly[d].count = cnt;
            for (int i = 0; i < cnt; i++) {
                r.hourly[d].temp_c[i]    = jarr(hourly, "temperature_2m", base + i, 0);
                r.hourly[d].precip_mm[i] = jarr(hourly, "precipitation", base + i, 0);
                /* "time" is local ISO ("2026-09-08T14:00"); the hour is at offset 11. */
                const cJSON *tv = cJSON_GetArrayItem(ht, base + i);
                const char *ts = (cJSON_IsString(tv) && tv->valuestring) ? tv->valuestring : NULL;
                r.hourly[d].hour[i] = (ts && strlen(ts) >= 13) ? atoi(ts + 11) : i;
            }
            r.hourly[d].valid = cnt > 1;
        }
    }

    cJSON_Delete(root);
    *out = r;
    return true;
}
