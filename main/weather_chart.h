/* Hourly temperature + precipitation chart.
 *
 * Implements the chart the design's weather_ui.h declares via weather_hourly_t.
 * The design's HTML original drew the temperature line and the precipitation bars
 * on one plot with two independent y-scales; two scales on one frame invent a
 * correlation that isn't in the data, so this port keeps the same visual intent
 * but stacks two panels sharing one x-axis instead. Each panel then carries a
 * single series against a single scale.
 *
 * Marks follow the house spec: 2px round-capped line, bars capped at 24px with a
 * 2px surface gap between neighbours, hairline solid gridlines one step off the
 * surface, and labels in text tokens rather than the series color.
 */
#ifndef WEATHER_CHART_H
#define WEATHER_CHART_H

#include "lvgl.h"
#include "weather_ui.h"

/* Creates an empty chart of the given size. Feed it with weather_chart_set_data(). */
lv_obj_t *weather_chart_create(lv_obj_t *parent, int32_t w, int32_t h);

/* Redraws the chart for one day's hourly series. `h` may be NULL, which blanks it.
 * `fahrenheit` only affects the two temperature labels — the geometry is unit-free. */
void weather_chart_set_data(lv_obj_t *chart, const weather_hourly_t *h, bool fahrenheit);

#endif
