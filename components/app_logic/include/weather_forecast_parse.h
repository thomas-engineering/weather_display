#ifndef WEATHER_FORECAST_PARSE_H
#define WEATHER_FORECAST_PARSE_H

#include <stdbool.h>

/* Hardware-free parser for an Open-Meteo /v1/forecast response, shared by the
 * firmware's weather worker (main/app_weather.c) and the host simulator's
 * --live mode. Mirrors WEATHER_UI_DAYS / WEATHER_UI_CHART_POINTS from
 * main/weather_ui.h without depending on that header, so app_logic stays
 * free of main/'s UI and IDF types (see CLAUDE.md's "Codeorganisation"). */

#define WFP_DAYS           7
#define WFP_HOURLY_POINTS 24

typedef struct {
    int   hour[WFP_HOURLY_POINTS];       /* 0-23, local time */
    float temp_c[WFP_HOURLY_POINTS];
    float precip_mm[WFP_HOURLY_POINTS];
    int   count;                         /* may be < WFP_HOURLY_POINTS for a partial day */
    bool  valid;
} wfp_hourly_t;

typedef struct {
    char  iso_date[12];
    int   weather_code;
    int   precip_pct;
    float temp_max_c, temp_min_c;
    float feels_max_c, feels_min_c;
    float wind_max_kmh;
} wfp_day_t;

typedef struct {
    int   utc_offset_sec;
    int   weather_code;
    int   humidity_pct;
    int   precip_pct;
    float temp_c, feels_like_c, wind_kmh;

    int      day_count;                  /* <= WFP_DAYS */
    wfp_day_t    days[WFP_DAYS];
    wfp_hourly_t hourly[WFP_DAYS];
} wfp_forecast_t;

/* Parses `json` (the body of an Open-Meteo /v1/forecast response requesting
 * the same `current`/`hourly`/`daily` fields as app_weather.c's do_refresh())
 * into `out`. Returns false if the JSON is malformed or is missing the
 * "current" or "daily" object it requires; `out` is left untouched in that
 * case. */
bool weather_forecast_parse(const char *json, wfp_forecast_t *out);

#endif
