#include "weather_icons.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- filled shapes -----------------------------------------------------------
 * LVGL cannot fill a Bezier path or a union of rotated bars. The moon, the wind
 * arrow, the lightning bolt, the sun with its rays and the snowflakes are
 * therefore rasterized into an A8 coverage mask (scanline fill, nonzero winding,
 * 4 sub-rows with exact horizontal coverage) and shown as a recoloured image.
 *
 * Each mask is rasterized once per pixel size, at exactly that size, and drawn
 * 1:1. That keeps strokes of only a few pixels (a ray is ~2 px wide on a 36 px
 * forecast tile) from being lost by down-scaling, and it costs a single image
 * per shape instead of one rotated object per ray or flake arm, each of which
 * needs its own transform layer at draw time. */

#define SHAPE_SUBROWS 4
#define SHAPE_BEZIER_STEPS 12
#define SHAPE_MAX_SEGMENTS 8
#define SHAPE_MAX_POINTS 256
#define SHAPE_MAX_CONTOURS 12
#define SHAPE_CACHE_MAX 24         /* shapes x pixel sizes in use (36, 64 and 72 px icons, 14 px arrow) */
#define SHAPE_CIRCLE_STEPS 32
#define SHAPE_CAP_STEPS 8          /* segments per semicircular end of a ray or flake arm */
#define ICON_BOX 64.0f             /* viewBox of WeatherIcon.dc.html */
#define ARROW_BOX 24.0f            /* viewBox of the wind arrow */
#define ARROW_ROTATION_TENTHS 10   /* LVGL rotation unit: 0.1 degree */
#define SUN_RAY_COUNT 8
#define FLAKE_ARM_COUNT 3          /* bars crossing at the center, evenly spread over 180 degrees */
#define FLAKE_MAX_COUNT 3
#define DEG_HALF_TURN 180.0f
#define DEG_QUARTER_TURN 90.0f

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
static const shape_t k_shape_moon = { ICON_BOX, { 38, 6 }, 6, {
    { true, { 24, 8, 14, 20, 14, 34 } }, { true, { 14, 49, 26, 60, 41, 60 } },
    { true, { 49, 60, 56, 56, 60, 50 } }, { true, { 56, 51, 53, 52, 49, 52 } },
    { true, { 35, 52, 24, 41, 24, 27 } }, { true, { 24, 18, 30, 10, 38, 6 } } } };

/* Smaller crescent behind the cloud for the partly-cloudy night, "M22 6 C14 8 9 15 9 23 ...". */
static const shape_t k_shape_moon_small = { ICON_BOX, { 22, 6 }, 6, {
    { true, { 14, 8, 9, 15, 9, 23 } }, { true, { 9, 31, 15, 36, 22, 36 } },
    { true, { 27, 36, 31, 33, 33, 29 } }, { true, { 31, 30, 29, 30, 27, 30 } },
    { true, { 20, 30, 15, 24, 15, 18 } }, { true, { 15, 13, 18, 9, 22, 6 } } } };

/* Lightning bolt of the storm icon. The design's outline "M37 38 L23 52 L31 52
 * L26 64 L43 47 L35 47 L41 38 Z" is 26 units tall; the firmware's cloud is
 * taller than the design's, so the same outline is scaled by 20/26 to fit under
 * it, centred on x 31 (the cloud's centre) and ending at the icon's bottom edge. */
static const shape_t k_shape_bolt = { ICON_BOX, { 34.1f, 44.0f }, 6, {
    { false, { 23.3f, 54.8f } }, { false, { 29.5f, 54.8f } }, { false, { 25.6f, 64.0f } },
    { false, { 38.7f, 50.9f } }, { false, { 32.5f, 50.9f } }, { false, { 37.2f, 44.0f } } } };

/* Wind arrow "M12 2 L19 20 L12 16 L5 20 Z". */
static const shape_t k_shape_arrow = { ARROW_BOX, { 12, 2 }, 3, {
    { false, { 19, 20 } }, { false, { 12, 16 } }, { false, { 5, 20 } } } };

typedef struct { float x, y; } pt_t;

/* A set of closed contours. All contours wind the same way, so a nonzero fill
 * is their union (overlapping flake arms merge instead of cancelling out). */
typedef struct {
    pt_t pts[SHAPE_MAX_POINTS];
    int n;
    int end[SHAPE_MAX_CONTOURS];   /* index one past each contour's last point */
    int count;
} poly_set_t;

static void poly_push(poly_set_t *p, float x, float y) {
    if (p->n < SHAPE_MAX_POINTS) p->pts[p->n++] = (pt_t){ x, y };
}

static void poly_close(poly_set_t *p) {
    if (p->count < SHAPE_MAX_CONTOURS) p->end[p->count++] = p->n;
}

static void poly_circle(poly_set_t *p, float cx, float cy, float r) {
    for (int i = 0; i < SHAPE_CIRCLE_STEPS; i++) {
        float a = 2.0f * (float)M_PI * (float)i / SHAPE_CIRCLE_STEPS;
        poly_push(p, cx + cosf(a) * r, cy + sinf(a) * r);
    }
    poly_close(p);
}

/* Rounded bar of `width` x `len` centred on (cx, cy), its long axis pointing
 * along `axis_deg`. Both ends are semicircles; the angle runs the same way as
 * in poly_circle(). */
static void poly_capsule(poly_set_t *p, float cx, float cy, float width, float len, float axis_deg) {
    float r = width / 2.0f;
    float half_run = len / 2.0f - r;
    float axis = axis_deg * (float)M_PI / DEG_HALF_TURN;
    float ux = cosf(axis), uy = sinf(axis);
    for (int end = 0; end < 2; end++) {
        float sign = end == 0 ? 1.0f : -1.0f;
        float ox = cx + ux * half_run * sign, oy = cy + uy * half_run * sign;
        float start = axis_deg - DEG_QUARTER_TURN + (float)end * DEG_HALF_TURN;
        for (int k = 0; k <= SHAPE_CAP_STEPS; k++) {
            float a = (start + DEG_HALF_TURN * (float)k / SHAPE_CAP_STEPS) * (float)M_PI / DEG_HALF_TURN;
            poly_push(p, ox + cosf(a) * r, oy + sinf(a) * r);
        }
    }
    poly_close(p);
}

typedef struct {
    float cx, cy;                  /* centre of the sun disc */
    float r;                       /* disc radius */
    float ray_w, ray_len;
    float ray_dist;                /* sun centre to ray centre */
} sun_geom_t;

/* Clear sky: "circle r 13, 8 rects 6 x 11 at y 2" of WeatherIcon.dc.html. */
static const sun_geom_t k_sun_clear = { 32.0f, 32.0f, 13.0f, 6.0f, 11.0f, 24.5f };
/* Partly cloudy. The design puts the disc at (20, 20) behind a cloud whose top
 * edge is at y 13; the firmware's cloud sits lower (top at 23), so the disc is
 * moved with it. Disc r 9 and rays 2.8 x 6 at distance 14.5 as in the design. */
static const sun_geom_t k_sun_partly = { 19.0f, 26.0f, 9.0f, 2.8f, 6.0f, 14.5f };

static void build_sun_poly(poly_set_t *p, const void *arg) {
    const sun_geom_t *g = arg;
    poly_circle(p, g->cx, g->cy, g->r);
    for (int i = 0; i < SUN_RAY_COUNT; i++) {
        float deg = 2.0f * DEG_HALF_TURN * (float)i / SUN_RAY_COUNT;
        float a = deg * (float)M_PI / DEG_HALF_TURN;
        poly_capsule(p, g->cx + cosf(a) * g->ray_dist, g->cy + sinf(a) * g->ray_dist, g->ray_w, g->ray_len, deg);
    }
}

typedef struct {
    int count;
    pt_t center[FLAKE_MAX_COUNT];
    float arm_len, arm_w;
} flakes_geom_t;

/* Three six-armed flakes, "3 bars of 14 x 3.2 per flake" of the design, laid out
 * under the firmware's snow cloud (bottom edge at y 44.8). The middle one sits
 * 2 units lower than the outer two. */
static const flakes_geom_t k_flakes = { 3, { { 12.0f, 55.0f }, { 31.0f, 57.0f }, { 51.0f, 55.0f } }, 14.0f, 3.2f };

static void build_flakes_poly(poly_set_t *p, const void *arg) {
    const flakes_geom_t *g = arg;
    for (int f = 0; f < g->count; f++)
        for (int arm = 0; arm < FLAKE_ARM_COUNT; arm++)
            poly_capsule(p, g->center[f].x, g->center[f].y, g->arm_w, g->arm_len,
                         DEG_HALF_TURN * (float)arm / FLAKE_ARM_COUNT);
}

/* Where a shape's contours come from: a Bezier/line outline or a generator. */
typedef struct {
    const shape_t *path;
    void (*build)(poly_set_t *p, const void *arg);
    const void *arg;
    float box;                     /* side of the square the contours live in */
} shape_src_t;

static const shape_src_t k_src_moon       = { .path = &k_shape_moon,       .box = ICON_BOX };
static const shape_src_t k_src_moon_small = { .path = &k_shape_moon_small, .box = ICON_BOX };
static const shape_src_t k_src_bolt       = { .path = &k_shape_bolt,       .box = ICON_BOX };
static const shape_src_t k_src_arrow      = { .path = &k_shape_arrow,      .box = ARROW_BOX };
static const shape_src_t k_src_sun_clear  = { .build = build_sun_poly, .arg = &k_sun_clear,  .box = ICON_BOX };
static const shape_src_t k_src_sun_partly = { .build = build_sun_poly, .arg = &k_sun_partly, .box = ICON_BOX };
static const shape_src_t k_src_flakes     = { .build = build_flakes_poly, .arg = &k_flakes, .box = ICON_BOX };

/* Flattens the path into one contour. */
static void flatten_shape(const shape_t *s, poly_set_t *p) {
    poly_push(p, s->start[0], s->start[1]);
    for (int i = 0; i < s->count; i++) {
        const shape_seg_t *g = &s->seg[i];
        pt_t p0 = p->pts[p->n - 1];
        if (!g->cubic) { poly_push(p, g->v[0], g->v[1]); continue; }
        for (int k = 1; k <= SHAPE_BEZIER_STEPS; k++) {
            float t = (float)k / SHAPE_BEZIER_STEPS, u = 1.0f - t;
            poly_push(p,
                u * u * u * p0.x + 3 * u * u * t * g->v[0] + 3 * u * t * t * g->v[2] + t * t * t * g->v[4],
                u * u * u * p0.y + 3 * u * u * t * g->v[1] + 3 * u * t * t * g->v[3] + t * t * t * g->v[5]);
        }
    }
    poly_close(p); /* the polygon closes from the last point back to the first */
}

typedef struct { float x; int dir; } crossing_t;

static int cmp_crossing(const void *a, const void *b) {
    float x = ((const crossing_t *)a)->x, y = ((const crossing_t *)b)->x;
    return (x > y) - (x < y);
}

/* Fills `mask` (px x px bytes) with the nonzero-winding coverage of `p`, whose
 * coordinates live in a `box`-sized square. False if out of memory. */
static bool rasterize_polys(const poly_set_t *p, float box, int px, uint8_t *mask) {
    float scale = (float)px / box;
    float *acc = lv_malloc(sizeof(float) * (size_t)px);
    crossing_t *xs = lv_malloc(sizeof(crossing_t) * (size_t)(p->n > 0 ? p->n : 1));
    if (!acc || !xs) { lv_free(acc); lv_free(xs); return false; }
    for (int py = 0; py < px; py++) {
        for (int x = 0; x < px; x++) acc[x] = 0.0f;
        for (int sub = 0; sub < SHAPE_SUBROWS; sub++) {
            float y = ((float)py + ((float)sub + 0.5f) / SHAPE_SUBROWS) / scale;
            int nx = 0, first = 0;
            for (int c = 0; c < p->count; c++) {
                int last = p->end[c];
                for (int i = first; i < last; i++) {
                    pt_t a = p->pts[i], b = p->pts[i + 1 < last ? i + 1 : first];
                    if ((a.y <= y) == (b.y <= y)) continue;
                    xs[nx].x = a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x);
                    xs[nx].dir = b.y > a.y ? 1 : -1;
                    nx++;
                }
                first = last;
            }
            qsort(xs, (size_t)nx, sizeof(crossing_t), cmp_crossing);
            int winding = 0;
            for (int i = 0; i + 1 < nx; i++) {
                winding += xs[i].dir;
                if (winding == 0) continue;
                float x0 = xs[i].x * scale, x1 = xs[i + 1].x * scale;
                if (x1 <= 0.0f || x0 >= (float)px) continue;
                if (x0 < 0.0f) x0 = 0.0f;
                if (x1 > (float)px) x1 = (float)px;
                for (int x = (int)x0; x <= (int)x1 && x < px; x++) {
                    float lo = x > x0 ? (float)x : x0, hi = (x + 1) < x1 ? (float)(x + 1) : x1;
                    if (hi > lo) acc[x] += (hi - lo) / SHAPE_SUBROWS;
                }
            }
        }
        for (int x = 0; x < px; x++) {
            float a = acc[x] > 1.0f ? 1.0f : acc[x];
            mask[py * px + x] = (uint8_t)(a * LV_OPA_COVER + 0.5f);
        }
    }
    lv_free(acc);
    lv_free(xs);
    return true;
}

typedef struct { const shape_src_t *src; int px; uint8_t *buf; lv_image_dsc_t dsc; } shape_cache_t;
static shape_cache_t s_shape_cache[SHAPE_CACHE_MAX];

/* Rasterizes on first use of a (shape, size) pair; keeps the mask for the life
 * of the program. NULL if out of memory or the cache is full (the shape is then
 * simply not drawn; the next call tries again). */
static const lv_image_dsc_t *shape_image(const shape_src_t *src, int px) {
    shape_cache_t *slot = NULL;
    for (int i = 0; i < SHAPE_CACHE_MAX; i++) {
        shape_cache_t *e = &s_shape_cache[i];
        if (e->src == src && e->px == px) return &e->dsc;
        if (!slot && !e->src) slot = e;
    }
    if (!slot || px <= 0) return NULL;
    poly_set_t *poly = lv_malloc(sizeof(poly_set_t));
    uint8_t *buf = lv_malloc((size_t)px * (size_t)px);
    bool ok = poly && buf;
    if (ok) {
        poly->n = 0;
        poly->count = 0;
        if (src->path) flatten_shape(src->path, poly);
        else src->build(poly, src->arg);
        ok = rasterize_polys(poly, src->box, px, buf);
    }
    lv_free(poly);
    if (!ok) { lv_free(buf); return NULL; }
    slot->src = src;
    slot->px = px;
    slot->buf = buf;
    slot->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    slot->dsc.header.cf = LV_COLOR_FORMAT_A8;
    slot->dsc.header.w = (uint32_t)px;
    slot->dsc.header.h = (uint32_t)px;
    slot->dsc.header.stride = (uint32_t)px;
    slot->dsc.data_size = (uint32_t)px * (uint32_t)px;
    slot->dsc.data = buf;
    return &slot->dsc;
}

/* A shape rasterized at `size` px square, placed at the top-left of `parent`, recoloured. */
static lv_obj_t *shape_obj(lv_obj_t *parent, const shape_src_t *src, int32_t size, lv_color_t color, lv_opa_t opa) {
    const lv_image_dsc_t *dsc = shape_image(src, size);
    if (!dsc) return NULL;
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, dsc);
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
    lv_obj_t *img = shape_obj(parent, &k_src_arrow, size, color, LV_OPA_COVER);
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

/* Nocturne neutral-600: the fog icon's cloud is a solid dark gray, independent
 * of the tint the caller passes in. */
#define FOG_CLOUD_COLOR lv_color_hex(0x75798C)

/* A classic "cloud from three circles + a base" silhouette, scaled to `size`. */
static void build_cloud(lv_obj_t *parent, int32_t size, lv_color_t c, lv_opa_t opa, float y_off) {
    int32_t base_h = size * 0.34f;
    dot(parent, size * 0.62f, base_h, size * 0.16f, size * (0.50f + y_off), c, base_h / 2, opa);
    dot(parent, size * 0.32f, size * 0.32f, size * 0.06f, size * (0.36f + y_off), c, LV_RADIUS_CIRCLE, opa);
    dot(parent, size * 0.40f, size * 0.40f, size * 0.30f, size * (0.22f + y_off), c, LV_RADIUS_CIRCLE, opa);
    dot(parent, size * 0.34f, size * 0.34f, size * 0.58f, size * (0.34f + y_off), c, LV_RADIUS_CIRCLE, opa);
}

static void build_icon(lv_obj_t *cont, weather_icon_t icon, int32_t size, lv_color_t color) {
    switch (icon) {
        case WX_ICON_CLEAR:
            shape_obj(cont, &k_src_sun_clear, size, color, LV_OPA_COVER);
            break;
        case WX_ICON_PARTLY:
            /* Sun and rays are one mask at the sun's 0.8 opacity; the cloud
             * drawn after it covers the part behind it. */
            shape_obj(cont, &k_src_sun_partly, size, color, LV_OPA_80);
            build_cloud(cont, size, color, LV_OPA_COVER, 0.14f);
            break;
        case WX_ICON_CLEAR_NIGHT:
            shape_obj(cont, &k_src_moon, size, color, LV_OPA_COVER);
            break;
        case WX_ICON_PARTLY_NIGHT:
            shape_obj(cont, &k_src_moon_small, size, color, LV_OPA_80);
            build_cloud(cont, size, color, LV_OPA_COVER, 0.14f);
            break;
        case WX_ICON_CLOUD:
            build_cloud(cont, size, color, LV_OPA_COVER, 0.0f);
            break;
        case WX_ICON_FOG:
            build_cloud(cont, size, FOG_CLOUD_COLOR, LV_OPA_COVER, -0.18f);
            dot(cont, size * 0.86f, size * 0.10f, size * 0.06f, size * 0.73f, color, size * 0.05f, LV_OPA_60);
            dot(cont, size * 0.62f, size * 0.10f, size * 0.06f, size * 0.90f, color, size * 0.05f, LV_OPA_40);
            break;
        case WX_ICON_RAIN: {
            /* FIX: two mismatches against WeatherIcon.dc.html's own rain
             * glyph (rects at y=45/48/45, all height=14, in its 64x64 viewBox;
             * since the 2026-11-02 design the x positions are 11.8/26.8/41.8). First, build_cloud()'s base bar (its lowest
             * element, with this y_off=-0.10) bottoms out at size*0.74 —
             * the drops used to start at size*0.72, overlapping the cloud's
             * base by 2% of the icon size instead of sitting clearly below
             * it. Second, the design staggers its middle drop 3/64 lower
             * than the outer two (same length, offset start, not a uniform
             * row) — this drew all three at the same y instead. */
            build_cloud(cont, size, color, LV_OPA_COVER, -0.10f);
            const float drop_y[3] = { 0.78f, 0.82f, 0.78f };
            for (int i = 0; i < 3; i++)
                dot(cont, size * 0.08f, size * 0.18f, size * (0.17f + i * 0.28f), size * drop_y[i], color, size * 0.04f, LV_OPA_COVER);
            break;
        }
        case WX_ICON_SNOW:
            build_cloud(cont, size, color, LV_OPA_COVER, -0.14f);
            shape_obj(cont, &k_src_flakes, size, color, LV_OPA_COVER);
            break;
        case WX_ICON_STORM:
            /* The cloud sits as high as the icon allows (its top touches y 0,
             * like the design's) so the filled bolt fits fully below it. */
            build_cloud(cont, size, color, LV_OPA_COVER, -0.22f);
            shape_obj(cont, &k_src_bolt, size, color, LV_OPA_COVER);
            break;
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
    /* FIX: build_icon() positions every glyph part as a fraction of `size`, but
     * the container itself keeps whatever size weather_icon_create() gave it
     * unless resized here too — a larger rebuild would otherwise be clipped by
     * the old container on its far (bottom/right) edges. */
    lv_obj_set_size(icon_obj, size, size);
    lv_obj_clean(icon_obj);
    build_icon(icon_obj, icon, size, color);
}
