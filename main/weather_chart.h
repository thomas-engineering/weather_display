/* Hourly temperature + precipitation chart.
 *
 * Matches the design's combined dual-axis plot (Weather App.dc.html,
 * buildDayChart()): one plot area, temperature as a line against the left
 * axis, precipitation as bars against the right axis (in mm, Open-Meteo's
 * own unit — there is no user-facing precipitation unit setting), hour
 * labels along the bottom, and a two-row legend (dot = temperature, square
 * = precipitation) to the left of the plot.
 *
 * Marks follow the house spec: 2px round-capped line, hairline solid axis
 * lines, and tick/legend text in the series' own color (temperature ticks
 * and the legend dot in --color-accent, precipitation ticks/bars/legend
 * square in --color-accent-700) rather than a neutral text token.
 */
#ifndef WEATHER_CHART_H
#define WEATHER_CHART_H

#include "lvgl.h"
#include "weather_ui.h"

/* Creates an empty chart of the given size. Feed it with weather_chart_set_data(). */
lv_obj_t *weather_chart_create(lv_obj_t *parent, int32_t w, int32_t h);

/* Redraws the chart for one day's hourly series. `h` may be NULL, which blanks
 * it (legend and axes go empty too). `fahrenheit` only affects the left-axis
 * tick text — the geometry is unit-free. `temp_label`/`precip_label` are the
 * legend captions (already localized — weather_strings[lang].temperature /
 * .precip), so the legend keeps up with a language change the same way the
 * rest of the chart data does whenever this is called again. */
void weather_chart_set_data(lv_obj_t *chart, const weather_hourly_t *h, bool fahrenheit,
                             const char *temp_label, const char *precip_label);

#endif
