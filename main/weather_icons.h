#ifndef WEATHER_ICONS_H
#define WEATHER_ICONS_H

#include "lvgl.h"
#include <stdbool.h>

typedef enum {
    WX_ICON_CLEAR = 0,
    WX_ICON_PARTLY,
    WX_ICON_CLOUD,
    WX_ICON_FOG,
    WX_ICON_RAIN,
    WX_ICON_SNOW,
    WX_ICON_STORM,
    /* 2026-10-19 sync: night variants of the two icons that show a sun. */
    WX_ICON_CLEAR_NIGHT,
    WX_ICON_PARTLY_NIGHT,
} weather_icon_t;

/* Maps an Open-Meteo WMO weather_code to one of the glyph categories above
 * (same bucketing as the HTML/DC version's WeatherIcon component). */
weather_icon_t weather_icon_from_wmo(int code);

/* Same as weather_icon_from_wmo(), but clear and partly-cloudy become the moon
 * variants when `night` is true (Open-Meteo's is_day == 0). All other
 * categories look the same day and night, like in the design. */
weather_icon_t weather_icon_from_wmo_night(int code, bool night);

/* Small filled arrow (the design's wind-direction glyph), tinted `color`, drawn
 * `size` px wide and tall pointing up; rotate it with weather_arrow_set_rotation().
 * Not clickable. */
lv_obj_t *weather_arrow_create(lv_obj_t *parent, int32_t size, lv_color_t color);

/* Rotation in whole degrees, clockwise, around the arrow's centre. */
void weather_arrow_set_rotation(lv_obj_t *arrow, int deg);

/* Builds a solid-glyph weather icon out of plain LVGL primitives (circles, rounded
 * rects, a polyline) at `size` px, tinted with `color` — no image assets required.
 * Returns the container object; destroy with lv_obj_delete() to remove it. */
lv_obj_t *weather_icon_create(lv_obj_t *parent, weather_icon_t icon, int32_t size, lv_color_t color);

/* Swaps the icon type on an existing icon container in place (rebuilds children). */
void weather_icon_set_type(lv_obj_t *icon_obj, weather_icon_t icon, int32_t size, lv_color_t color);

#endif
