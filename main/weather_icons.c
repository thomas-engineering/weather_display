#include "weather_icons.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- filled shapes from the design's SVG paths ------------------------------
 * LVGL cannot fill a Bezier path, so the moon and the wind arrow are rasterized
 * once into an A8 coverage mask (scanline fill, 4 sub-rows with exact horizontal
 * coverage) and shown as a recoloured, stretched image. */

#define SHAPE_MASK_PX 128          /* mask resolution; the image is stretched to the icon size */
#define SHAPE_SUBROWS 4
#define SHAPE_BEZIER_STEPS 12
#define SHAPE_MAX_SEGMENTS 8
#define SHAPE_MAX_POINTS (SHAPE_MAX_SEGMENTS * SHAPE_BEZIER_STEPS + 2)
#define MOON_BOX 64.0f             /* viewBox of WeatherIcon.dc.html */
#define ARROW_BOX 24.0f            /* viewBox of the wind arrow */
#define ARROW_ROTATION_TENTHS 10   /* LVGL rotation unit: 0.1 degree */
#define SCALE_FULL 256             /* lv_image scale 256 = 100 % */

typedef struct {
    bool cubic;                    /* true: v = c1 c2 end, false: v[0..1] = end (straight line) */
    float v[6];
} shape_seg_t;

typedef struct {
    float box;                     /* side of the square the coordinates live in */
    float start[2];
    int count;
    shape_seg_t seg[SHAPE_MAX_SEGMENTS];
} shape_t;

/* Crescent for the clear night, path "M38 6 C24 8 14 20 14 34 ..." of WeatherIcon.dc.html. */
static const shape_t k_shape_moon = { MOON_BOX, { 38, 6 }, 6, {
    { true, { 24, 8, 14, 20, 14, 34 } }, { true, { 14, 49, 26, 60, 41, 60 } },
    { true, { 49, 60, 56, 56, 60, 50 } }, { true, { 56, 51, 53, 52, 49, 52 } },
    { true, { 35, 52, 24, 41, 24, 27 } }, { true, { 24, 18, 30, 10, 38, 6 } } } };

/* Smaller crescent behind the cloud for the partly-cloudy night, "M22 6 C14 8 9 15 9 23 ...". */
static const shape_t k_shape_moon_small = { MOON_BOX, { 22, 6 }, 6, {
    { true, { 14, 8, 9, 15, 9, 23 } }, { true, { 9, 31, 15, 36, 22, 36 } },
    { true, { 27, 36, 31, 33, 33, 29 } }, { true, { 31, 30, 29, 30, 27, 30 } },
    { true, { 20, 30, 15, 24, 15, 18 } }, { true, { 15, 13, 18, 9, 22, 6 } } } };

/* Wind arrow "M12 2 L19 20 L12 16 L5 20 Z". */
static const shape_t k_shape_arrow = { ARROW_BOX, { 12, 2 }, 3, {
    { false, { 19, 20 } }, { false, { 12, 16 } }, { false, { 5, 20 } } } };

typedef struct { float x, y; } pt_t;

static int flatten_shape(const shape_t *s, pt_t *out) {
    int n = 0;
    out[n++] = (pt_t){ s->start[0], s->start[1] };
    for (int i = 0; i < s->count; i++) {
        const shape_seg_t *g = &s->seg[i];
        pt_t p0 = out[n - 1];
        if (!g->cubic) { out[n++] = (pt_t){ g->v[0], g->v[1] }; continue; }
        for (int k = 1; k <= SHAPE_BEZIER_STEPS; k++) {
            float t = (float)k / SHAPE_BEZIER_STEPS, u = 1.0f - t;
            out[n].x = u * u * u * p0.x + 3 * u * u * t * g->v[0] + 3 * u * t * t * g->v[2] + t * t * t * g->v[4];
            out[n].y = u * u * u * p0.y + 3 * u * u * t * g->v[1] + 3 * u * t * t * g->v[3] + t * t * t * g->v[5];
            n++;
        }
    }
    return n; /* the polygon closes from the last point back to the first */
}

static int cmp_float(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* Fills `mask` (SHAPE_MASK_PX^2 bytes) with the shape's even-odd coverage. */
static void rasterize_shape(const shape_t *s, uint8_t *mask) {
    pt_t pts[SHAPE_MAX_POINTS];
    int n = flatten_shape(s, pts);
    float scale = SHAPE_MASK_PX / s->box;
    float *acc = lv_malloc(sizeof(float) * SHAPE_MASK_PX);
    if (!acc) { memset(mask, 0, SHAPE_MASK_PX * SHAPE_MASK_PX); return; }
    for (int py = 0; py < SHAPE_MASK_PX; py++) {
        for (int x = 0; x < SHAPE_MASK_PX; x++) acc[x] = 0.0f;
        for (int sub = 0; sub < SHAPE_SUBROWS; sub++) {
            float y = (py + (sub + 0.5f) / SHAPE_SUBROWS) / scale;
            float xs[SHAPE_MAX_POINTS];
            int nx = 0;
            for (int i = 0; i < n; i++) {
                pt_t a = pts[i], b = pts[(i + 1) % n];
                if ((a.y <= y) == (b.y <= y)) continue;
                xs[nx++] = a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x);
            }
            qsort(xs, nx, sizeof(float), cmp_float);
            for (int i = 0; i + 1 < nx; i += 2) {
                float x0 = xs[i] * scale, x1 = xs[i + 1] * scale;
                if (x1 <= 0.0f || x0 >= SHAPE_MASK_PX) continue;
                if (x0 < 0.0f) x0 = 0.0f;
                if (x1 > SHAPE_MASK_PX) x1 = SHAPE_MASK_PX;
                for (int x = (int)x0; x <= (int)x1 && x < SHAPE_MASK_PX; x++) {
                    float lo = x > x0 ? (float)x : x0, hi = (x + 1) < x1 ? (float)(x + 1) : x1;
                    if (hi > lo) acc[x] += (hi - lo) / SHAPE_SUBROWS;
                }
            }
        }
        for (int x = 0; x < SHAPE_MASK_PX; x++) {
            float a = acc[x] > 1.0f ? 1.0f : acc[x];
            mask[py * SHAPE_MASK_PX + x] = (uint8_t)(a * LV_OPA_COVER + 0.5f);
        }
    }
    lv_free(acc);
}

typedef struct { const shape_t *shape; lv_image_dsc_t dsc; uint8_t *buf; } shape_img_t;
static shape_img_t s_moon = { .shape = &k_shape_moon }, s_moon_small = { .shape = &k_shape_moon_small },
                     s_arrow = { .shape = &k_shape_arrow };

/* Rasterizes on first use; keeps the mask for the life of the program. NULL if out of memory. */
static const lv_image_dsc_t *shape_image(shape_img_t *si) {
    if (!si->buf) {
        si->buf = lv_malloc(SHAPE_MASK_PX * SHAPE_MASK_PX);
        if (!si->buf) return NULL;
        rasterize_shape(si->shape, si->buf);
        si->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        si->dsc.header.cf = LV_COLOR_FORMAT_A8;
        si->dsc.header.w = SHAPE_MASK_PX;
        si->dsc.header.h = SHAPE_MASK_PX;
        si->dsc.header.stride = SHAPE_MASK_PX;
        si->dsc.data_size = SHAPE_MASK_PX * SHAPE_MASK_PX;
        si->dsc.data = si->buf;
    }
    return &si->dsc;
}

/* A shape stretched to `size` px square at the top-left of `parent`, recoloured. */
static lv_obj_t *shape_obj(lv_obj_t *parent, shape_img_t *si, int32_t size, lv_color_t color, lv_opa_t opa) {
    const lv_image_dsc_t *dsc = shape_image(si);
    if (!dsc) return NULL;
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, dsc);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_size(img, size, size);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_set_style_image_recolor(img, color, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(img, opa, 0);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE); /* decorative, see dot() */
    lv_obj_remove_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    return img;
}

lv_obj_t *weather_arrow_create(lv_obj_t *parent, int32_t size, lv_color_t color) {
    lv_obj_t *img = shape_obj(parent, &s_arrow, size, color, LV_OPA_COVER);
    if (!img) return NULL;
    lv_obj_set_style_transform_pivot_x(img, size / 2, 0);
    lv_obj_set_style_transform_pivot_y(img, size / 2, 0);
    return img;
}

void weather_arrow_set_rotation(lv_obj_t *arrow, int deg) {
    if (arrow) lv_obj_set_style_transform_rotation(arrow, deg * ARROW_ROTATION_TENTHS, 0);
}

weather_icon_t weather_icon_from_wmo_night(int code, bool night) {
    weather_icon_t icon = weather_icon_from_wmo(code);
    if (!night) return icon;
    if (icon == WX_ICON_CLEAR) return WX_ICON_CLEAR_NIGHT;
    if (icon == WX_ICON_PARTLY) return WX_ICON_PARTLY_NIGHT;
    return icon;
}

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
        case WX_ICON_CLEAR_NIGHT:
            shape_obj(cont, &s_moon, size, color, LV_OPA_COVER);
            break;
        case WX_ICON_PARTLY_NIGHT:
            shape_obj(cont, &s_moon_small, size, color, LV_OPA_80);
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
        case WX_ICON_RAIN: {
            /* FIX: two mismatches against WeatherIcon.dc.html's own rain
             * glyph (rects at x=15/30/45, y=45/48/45, all height=14, in its
             * 64x64 viewBox). First, build_cloud()'s base bar (its lowest
             * element, with this y_off=-0.10) bottoms out at size*0.74 —
             * the drops used to start at size*0.72, overlapping the cloud's
             * base by 2% of the icon size instead of sitting clearly below
             * it. Second, the design staggers its middle drop 3/64 lower
             * than the outer two (same length, offset start, not a uniform
             * row) — this drew all three at the same y instead. */
            build_cloud(cont, size, color, LV_OPA_COVER, -0.10f);
            const float drop_y[3] = { 0.78f, 0.82f, 0.78f };
            for (int i = 0; i < 3; i++)
                dot(cont, size * 0.08f, size * 0.18f, size * (0.22f + i * 0.28f), size * drop_y[i], color, size * 0.04f, LV_OPA_COVER);
            break;
        }
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
