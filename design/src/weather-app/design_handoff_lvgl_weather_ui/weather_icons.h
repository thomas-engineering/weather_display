#ifndef WEATHER_ICONS_H
#define WEATHER_ICONS_H

#include "lvgl.h"

typedef enum {
    WX_ICON_CLEAR = 0,
    WX_ICON_PARTLY,
    WX_ICON_CLOUD,
    WX_ICON_FOG,
    WX_ICON_RAIN,
    WX_ICON_SNOW,
    WX_ICON_STORM,
} weather_icon_t;

/* Maps an Open-Meteo WMO weather_code to one of the glyph categories above
 * (same bucketing as the HTML/DC version's WeatherIcon component). */
weather_icon_t weather_icon_from_wmo(int code);

/* Builds a solid-glyph weather icon out of plain LVGL primitives (circles, rounded
 * rects, a polyline) at `size` px, tinted with `color` — no image assets required.
 * Returns the container object; destroy with lv_obj_del() to remove it. */
lv_obj_t *weather_icon_create(lv_obj_t *parent, weather_icon_t icon, lv_coord_t size, lv_color_t color);

/* Swaps the icon type on an existing icon container in place (rebuilds children). */
void weather_icon_set_type(lv_obj_t *icon_obj, weather_icon_t icon, lv_coord_t size, lv_color_t color);

#endif
