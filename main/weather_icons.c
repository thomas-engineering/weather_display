#include "weather_icons.h"
#include <math.h>

weather_icon_t weather_icon_from_wmo(int code) {
    if (code == 0) return WX_ICON_CLEAR;
    if (code == 1 || code == 2) return WX_ICON_PARTLY;
    if (code == 3) return WX_ICON_CLOUD;
    if (code == 45 || code == 48) return WX_ICON_FOG;
    switch (code) {
        case 51: case 53: case 55: case 56: case 57:
        case 61: case 63: case 65: case 66: case 67:
        case 80: case 81: case 82:
            return WX_ICON_RAIN;
        case 71: case 73: case 75: case 77: case 85: case 86:
            return WX_ICON_SNOW;
        case 95: case 96: case 99:
            return WX_ICON_STORM;
        default:
            return WX_ICON_CLOUD;
    }
}

static lv_obj_t *dot(lv_obj_t *parent, int32_t w, int32_t h, int32_t x, int32_t y, lv_color_t c, int32_t radius, lv_opa_t opa) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    /* FIX: lv_obj_create() defaults to CLICKABLE. These shapes are purely
     * decorative, but left clickable they silently swallow taps meant for
     * whatever real widget sits behind the icon (e.g. a forecast tile's day
     * card) instead of falling through to it — and since weather_icon_set_type()
     * tears down and rebuilds every dot on each data refresh, a bubble-event
     * flag added once at UI-build time doesn't survive either. Not clickable
     * is the correct, rebuild-proof fix. */
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* A classic "cloud from three circles + a base" silhouette, scaled to `size`. */
static void build_cloud(lv_obj_t *parent, int32_t size, lv_color_t c, lv_opa_t opa, float y_off) {
    int32_t base_h = size * 0.34f;
    dot(parent, size * 0.62f, base_h, size * 0.16f, size * (0.50f + y_off), c, base_h / 2, opa);
    dot(parent, size * 0.32f, size * 0.32f, size * 0.06f, size * (0.36f + y_off), c, LV_RADIUS_CIRCLE, opa);
    dot(parent, size * 0.40f, size * 0.40f, size * 0.30f, size * (0.22f + y_off), c, LV_RADIUS_CIRCLE, opa);
    dot(parent, size * 0.34f, size * 0.34f, size * 0.58f, size * (0.34f + y_off), c, LV_RADIUS_CIRCLE, opa);
}

static void bolt_free_cb(lv_event_t *e) {
    lv_free(lv_obj_get_user_data(lv_event_get_target_obj(e)));
}

static void build_icon(lv_obj_t *cont, weather_icon_t icon, int32_t size, lv_color_t color) {
    switch (icon) {
        case WX_ICON_CLEAR: {
            /* FIX: matches WeatherIcon.dc.html's actual sun glyph (64px viewBox:
             * circle r=13, 8 copies of one rounded rect rotated 0/45/.../315
             * about the circle's own center) — the previous version placed 8
             * axis-aligned ovals around the circle but never rotated the ovals
             * themselves, so diagonal rays looked like stray vertical blobs
             * instead of pointing outward like the design's sunburst. */
            int32_t core_d = size * 0.40625f;   /* 26/64 */
            dot(cont, core_d, core_d, (size - core_d) / 2, (size - core_d) / 2, color, LV_RADIUS_CIRCLE, LV_OPA_COVER);
            int32_t ray_w = size * 0.09375f, ray_len = size * 0.171875f;      /* 6/64, 11/64 */
            float dist = size * 0.3828125f;                                  /* center-to-ray-center, 24.5/64 */
            for (int i = 0; i < 8; i++) {
                float a = (float)i * (float)M_PI / 4.0f;
                float cx = size / 2.0f + cosf(a) * dist;
                float cy = size / 2.0f + sinf(a) * dist;
                lv_obj_t *ray = dot(cont, ray_w, ray_len, (int32_t)(cx - ray_w / 2), (int32_t)(cy - ray_len / 2),
                                     color, ray_w / 2, LV_OPA_COVER);
                /* The design's un-rotated rect already sits at the "up" position
                 * (our a = 270°/i=6) pointing outward along -y; turning it to
                 * point along this ray's own radius needs the same +90° offset
                 * from its placement angle at every i. Pivot must be the ray's
                 * own center (not the default top-left) since its position is
                 * already placed on the circle above. */
                lv_obj_set_style_transform_pivot_x(ray, ray_w / 2, 0);
                lv_obj_set_style_transform_pivot_y(ray, ray_len / 2, 0);
                lv_obj_set_style_transform_rotation(ray, ((i * 45 + 90) % 360) * 10, 0);
            }
            break;
        }
        case WX_ICON_PARTLY:
            dot(cont, size * 0.34f, size * 0.34f, size * 0.06f, size * 0.02f, color, LV_RADIUS_CIRCLE, LV_OPA_80);
            build_cloud(cont, size, color, LV_OPA_COVER, 0.14f);
            break;
        case WX_ICON_CLOUD:
            build_cloud(cont, size, color, LV_OPA_COVER, 0.0f);
            break;
        case WX_ICON_FOG:
            build_cloud(cont, size, color, LV_OPA_70, -0.14f);
            dot(cont, size * 0.86f, size * 0.10f, size * 0.06f, size * 0.70f, color, size * 0.05f, LV_OPA_60);
            dot(cont, size * 0.62f, size * 0.10f, size * 0.06f, size * 0.86f, color, size * 0.05f, LV_OPA_40);
            break;
        case WX_ICON_RAIN:
            build_cloud(cont, size, color, LV_OPA_COVER, -0.10f);
            for (int i = 0; i < 3; i++)
                dot(cont, size * 0.08f, size * 0.24f, size * (0.22f + i * 0.28f), size * 0.72f, color, size * 0.04f, LV_OPA_COVER);
            break;
        case WX_ICON_SNOW:
            build_cloud(cont, size, color, LV_OPA_COVER, -0.10f);
            for (int i = 0; i < 3; i++)
                dot(cont, size * 0.12f, size * 0.12f, size * (0.20f + i * 0.30f), size * 0.76f, color, LV_RADIUS_CIRCLE, LV_OPA_COVER);
            break;
        case WX_ICON_STORM: {
            build_cloud(cont, size, color, LV_OPA_COVER, -0.14f);
            /* One bolt array per icon object: the design export shared a single
             * `static` array across every storm glyph, so a second storm icon
             * silently rewrote the first one's points. lv_line does not copy the
             * point array, so it has to stay alive as long as the line does —
             * hang it off the line object and free it when the line is deleted. */
            lv_point_precise_t *bolt = lv_malloc(sizeof(lv_point_precise_t) * 5);
            if (!bolt) break;
            bolt[0].x = size * 0.56f; bolt[0].y = size * 0.62f;
            bolt[1].x = size * 0.36f; bolt[1].y = size * 0.82f;
            bolt[2].x = size * 0.50f; bolt[2].y = size * 0.82f;
            bolt[3].x = size * 0.34f; bolt[3].y = size * 1.02f;
            bolt[4].x = size * 0.64f; bolt[4].y = size * 0.78f;
            lv_obj_t *line = lv_line_create(cont);
            lv_line_set_points(line, bolt, 5);
            lv_obj_set_user_data(line, bolt);
            lv_obj_add_event_cb(line, bolt_free_cb, LV_EVENT_DELETE, NULL);
            lv_obj_set_style_line_color(line, color, 0);
            lv_obj_set_style_line_width(line, size * 0.08f, 0);
            lv_obj_set_style_line_rounded(line, true, 0);
            break;
        }
    }
}

lv_obj_t *weather_icon_create(lv_obj_t *parent, weather_icon_t icon, int32_t size, lv_color_t color) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, size, size);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_CLICKABLE);   /* FIX: see the comment in dot() */
    build_icon(cont, icon, size, color);
    return cont;
}

void weather_icon_set_type(lv_obj_t *icon_obj, weather_icon_t icon, int32_t size, lv_color_t color) {
    /* FIX: build_icon() positions every ray/cloud blob as a fraction of `size`,
     * but the container itself keeps whatever size weather_icon_create() gave
     * it unless resized here too. weather_ui.c's current-weather icon is
     * created at 64 and updated at 72 on every refresh — with no resize, the
     * rays drawn for a 72px layout got clipped by the still-64px container on
     * their far (bottom/right) edges, cutting off the sun icon's rays. */
    lv_obj_set_size(icon_obj, size, size);
    lv_obj_clean(icon_obj);
    build_icon(icon_obj, icon, size, color);
}
