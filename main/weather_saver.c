#include "weather_saver.h"
#include "weather_saver_digits.h"
#include "ui_fonts.h"
#include <math.h>
#include <string.h>

/* Geometry from the design's saver block (px). */
#define TUBE_W 124
#define TUBE_H 300
#define TUBE_GAP 6
#define GLASS_BOTTOM_INSET 22
#define GLASS_BORDER_OPA 56
#define GLASS_FILL_OPA 8
#define CAP_W 10
#define CAP_H 9
#define CAGE_X 21
#define CAGE_Y 56
#define CAGE_W (TUBE_W - 2 * CAGE_X)
#define CAGE_H 196
#define WINDOW_X 29
#define WINDOW_Y 64
#define PIN_SIZE 8
#define PIN_X 58
#define PIN_TOP_Y 48
#define PIN_BOTTOM_Y 252
#define BASE_X 3
#define BASE_H 16
#define SHINE_L_X 14
#define SHINE_L_Y 30
#define SHINE_L_W 6
#define SHINE_L_H 210
#define SHINE_R_X (TUBE_W - 10 - 3)
#define SHINE_R_Y 40
#define SHINE_R_W 3
#define SHINE_R_H 180
#define GROUP_W (2 * TUBE_W + TUBE_GAP)
#define ROW_GAP 16
#define DOT_SIZE 9
#define DOT_GAP 30
#define DOT_BOTTOM_MARGIN 22
#define DOT_HALO_LAYERS 4
/* Cage backdrop: radial-gradient(65% 55% at 50% 50%, in, out 85%), drawn exactly
 * (LVGL's radial gradients are circles only) and dithered, since RGB565 bands badly. */
#define CAGE_RX_PCT 65
#define CAGE_RY_PCT 55
#define CAGE_STOP_PCT 85
#define PCT_DIV 100.0f
#define CAGE_RING_OUTER_PX 1
#define CAGE_GLOW_WIDTH 40
#define CAGE_GLOW_OPA LV_OPA_20
#define BAYER_SIZE 4
#define RGB565_R_STEP 8.0f /* 8-bit levels per 5-bit step */
#define RGB565_G_STEP 4.0f /* ... per 6-bit step */
/* Screen background: radial-gradient(120% 140% at 20% -10%, in, out 55%) for the
 * theme style, (80% 60% at 50% 45%, in, out 75%) for orange; ellipses, so also pre-rendered. */
#define BG_THEME_CX_PCT 20
#define BG_THEME_CY_PCT (-10)
#define BG_THEME_RX_PCT 120
#define BG_THEME_RY_PCT 140
#define BG_THEME_STOP_PCT 55
#define BG_ORANGE_CX_PCT 50
#define BG_ORANGE_CY_PCT 45
#define BG_ORANGE_RX_PCT 80
#define BG_ORANGE_RY_PCT 60
#define BG_ORANGE_STOP_PCT 75
/* Wire mesh over the digits: three line sets at 0/60/120 degrees, 4 px gap + 0.6 px line. */
#define MESH_GAP_PX 4.0f
#define MESH_PERIOD_PX 4.6f
#define MESH_OPA LV_OPA_50
#define MESH_SUBSAMPLES 3
#define MESH_ANGLE_COUNT 3
#define MESH_ANGLE_STEP_DEG 60
#define DEG_TO_RAD 0.01745329252f
#define DOT_CORE_SIZE 4 /* the design's gradient is near-white over the inner 25 % */
#define CLOCK_PAD_X 30
#define CLOCK_W (3 * GROUP_W + 2 * DOT_SIZE + 4 * ROW_GAP + 2 * CLOCK_PAD_X)
#define BAR_Y 270
#define BAR_H 46
#define BAR_RADIUS 6
#define BAR_STOPS 4
#define BAR_LIGHT_EDGE_FRAC 17      /* 3 px of the 46 px bar, in 1/255 */
#define BAR_SHADE_START_FRAC 178    /* the bottom darkening starts at 70 % */
#define BAR_SHADE_OPA 89            /* 35 % black at the bottom edge */
#define BAR_GRAIN_PERIOD_PX 21      /* orange bar: fine grain along the bar's height */
#define BAR_GRAIN_LONG_PERIOD_PX 330 /* ... and broad bands along its length */
#define BAR_GRAIN_STOPS 10
#define BAR_INNER_LIGHT_OPA_THEME 64
#define BAR_INNER_LIGHT_OPA_ORANGE 46
#define BAR_SHADOW_OFFSET_THEME 16
#define BAR_SHADOW_WIDTH_THEME 40
#define BAR_SHADOW_OPA_THEME 166
#define BAR_SHADOW_OFFSET_ORANGE 14
#define BAR_SHADOW_WIDTH_ORANGE 30
#define BAR_SHADOW_OPA_ORANGE 140
#define BAR_EDGE_DROP_PX 2
#define CLOCK_BLOCK_H (BAR_Y + BAR_H)
#define DATE_ROW_PAD_Y 32
#define SEPARATOR_SIZE 4
#define DATE_GAP 16
#define DATE_ROW_H (FONT_22->line_height + 2 * DATE_ROW_PAD_Y)
#define SCREEN_W 1024
#define SCREEN_H 600
#define LIT_LAYERS 5

/* Tube-local lit stroke layers, widest/dimmest first (the design blurs these;
 * stacked translucent strokes stand in for the blur). */
static const struct { uint8_t width; uint8_t opa; uint8_t color_idx; } k_layers[LIT_LAYERS] = {
    { 26, LV_OPA_20, 0 }, { 16, LV_OPA_40, 0 }, { 8, LV_OPA_80, 1 }, { 3, LV_OPA_COVER, 2 }, { 2, LV_OPA_COVER, 3 },
};
#define GHOST_WIDTH 1
#define GHOST_OPA LV_OPA_50
#define DIGIT_NONE 0xFF

typedef struct {
    lv_color_t bg, bar, cage_in, cage_out, cage_ring, cage_ring_outer, cage_glow, pin1, pin2, ghost, mesh;
    lv_color_t bg_in, bg_out; /* background radial gradient */
    lv_color_t glow[4];       /* g1..g4 */
    lv_color_t dot_core, dot, dot_halo[DOT_HALO_LAYERS], text, text_glow;
} saver_palette_t;

/* "App theme": Nocturne tokens (design/src/nocturne/styles.css). */
static saver_palette_t palette_theme(void) {
    return (saver_palette_t){
        .bg = lv_color_hex(0x161826), .bar = lv_color_hex(0x3F424D),
        .cage_in = lv_color_hex(0x423A6A), .cage_out = lv_color_hex(0x292B31),
        .cage_ring = lv_color_hex(0x595D6C), .cage_ring_outer = lv_color_hex(0x292B31), .cage_glow = lv_color_hex(0x9184D9),
        .bg_in = lv_color_hex(0x2B2741), .bg_out = lv_color_hex(0x161826), .pin1 = lv_color_hex(0x595D6C), .pin2 = lv_color_hex(0x3F424D),
        .ghost = lv_color_hex(0x9397AB), .mesh = lv_color_hex(0x75798C),
        .glow = { lv_color_hex(0x796CBF), lv_color_hex(0x9184D9), lv_color_hex(0xD2CEFD), lv_color_hex(0xF5F4FF) },
        .dot_core = lv_color_hex(0xF5F4FF), .dot = lv_color_hex(0xD2CEFD),
        .dot_halo = { lv_color_hex(0xD2CEFD), lv_color_hex(0x9184D9), lv_color_hex(0x796CBF), lv_color_hex(0x5D5294) },
        .text = lv_color_hex(0xD2CEFD), .text_glow = lv_color_hex(0x9184D9),
    };
}

/* "Nixie orange": the design's oklch values converted by tools/gen_nixie_paths.py. */
static saver_palette_t palette_orange(void) {
    return (saver_palette_t){
        .bg = lv_color_hex(0x18120F), .bar = lv_color_hex(0x452818),
        .cage_in = lv_color_hex(0x7F230B), .cage_out = lv_color_hex(0x290C06),
        .cage_ring = lv_color_hex(0x37322E), .cage_ring_outer = lv_color_hex(0x1A1512), .cage_glow = lv_color_hex(0xC43B15),
        .bg_in = lv_color_hex(0x18120F), .bg_out = lv_color_hex(0x292B31), .pin1 = lv_color_hex(0x322D29), .pin2 = lv_color_hex(0x28231F),
        .ghost = lv_color_hex(0x8A7462), .mesh = lv_color_hex(0x564B42),
        .glow = { lv_color_hex(0xE03000), lv_color_hex(0xFF6A10), lv_color_hex(0xFFA040), lv_color_hex(0xFFE6B0) },
        .dot_core = lv_color_hex(0xFFE0A4), .dot = lv_color_hex(0xFF8E41),
        .dot_halo = { lv_color_hex(0xFF7F34), lv_color_hex(0xF34F08), lv_color_hex(0xDB3008), lv_color_hex(0xC21B0D) },
        .text = lv_color_hex(0xB44400), .text_glow = lv_color_hex(0xE03000),
    };
}

/* One alpha mask shared by all tubes; recoloured per style. */
static lv_image_dsc_t s_mesh_img;
static uint8_t *s_mesh_buf;

static void mesh_ensure(void) {
    if (s_mesh_buf) return;
    s_mesh_buf = lv_malloc(CAGE_W * CAGE_H);
    if (!s_mesh_buf) return;
    for (int y = 0; y < CAGE_H; y++) {
        for (int x = 0; x < CAGE_W; x++) {
            int hits = 0;
            for (int sy = 0; sy < MESH_SUBSAMPLES; sy++) {
                for (int sx = 0; sx < MESH_SUBSAMPLES; sx++) {
                    float px = x + (sx + 0.5f) / MESH_SUBSAMPLES, py = y + (sy + 0.5f) / MESH_SUBSAMPLES;
                    bool on = false;
                    for (int a = 0; a < MESH_ANGLE_COUNT && !on; a++) {
                        float rad = a * MESH_ANGLE_STEP_DEG * DEG_TO_RAD; /* CSS angle: 0 = towards the top */
                        float along = px * sinf(rad) - py * cosf(rad);
                        along -= MESH_PERIOD_PX * floorf(along / MESH_PERIOD_PX);
                        on = along >= MESH_GAP_PX;
                    }
                    if (on) hits++;
                }
            }
            s_mesh_buf[y * CAGE_W + x] = (uint8_t)(LV_OPA_COVER * hits / (MESH_SUBSAMPLES * MESH_SUBSAMPLES));
        }
    }
    s_mesh_img.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_mesh_img.header.cf = LV_COLOR_FORMAT_A8;
    s_mesh_img.header.w = CAGE_W;
    s_mesh_img.header.h = CAGE_H;
    s_mesh_img.header.stride = CAGE_W;
    s_mesh_img.data_size = CAGE_W * CAGE_H;
    s_mesh_img.data = s_mesh_buf;
}

static lv_image_dsc_t s_cage_img;
static uint8_t *s_cage_buf; /* RGB565, CAGE_W x CAGE_H */
static saver_style_t s_cage_style = SAVER_STYLE_COUNT;

static const uint8_t k_bayer[BAYER_SIZE][BAYER_SIZE] = {
    { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
};

static uint16_t dither_rgb565(float r, float g, float b, int x, int y) {
    float t = (k_bayer[y % BAYER_SIZE][x % BAYER_SIZE] + 0.5f) / (BAYER_SIZE * BAYER_SIZE); /* 0..1 */
    int rq = (int)((r + t * RGB565_R_STEP) / RGB565_R_STEP), gq = (int)((g + t * RGB565_G_STEP) / RGB565_G_STEP),
        bq = (int)((b + t * RGB565_R_STEP) / RGB565_R_STEP);
    if (rq > 31) rq = 31;
    if (gq > 63) gq = 63;
    if (bq > 31) bq = 31;
    return (uint16_t)((rq << 11) | (gq << 5) | bq);
}

/* Fills an RGB565 buffer with CSS-style radial-gradient(rx ry at cx cy, in, out stop): colour `in`
 * at the centre, `out` from `stop` (fraction of the ellipse) outwards, dithered. */
static void radial_fill(uint16_t *px, int w, int h, float cx, float cy, float rx, float ry, float stop,
                        lv_color_t c_in, lv_color_t c_out) {
    lv_color32_t a = lv_color_to_32(c_in, LV_OPA_COVER), z = lv_color_to_32(c_out, LV_OPA_COVER);
    for (int y = 0; y < h; y++) {
        float dy = (y + 0.5f - cy) / ry;
        for (int x = 0; x < w; x++) {
            float dx = (x + 0.5f - cx) / rx;
            float k = sqrtf(dx * dx + dy * dy) / stop;
            if (k > 1.0f) k = 1.0f;
            px[y * w + x] = dither_rgb565(a.red + (z.red - a.red) * k, a.green + (z.green - a.green) * k,
                                          a.blue + (z.blue - a.blue) * k, x, y);
        }
    }
}

static void image_dsc_rgb565(lv_image_dsc_t *d, uint16_t *px, int w, int h) {
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = w;
    d->header.h = h;
    d->header.stride = w * sizeof(uint16_t);
    d->data_size = w * h * sizeof(uint16_t);
    d->data = (const uint8_t *)px;
}

static lv_image_dsc_t s_bg_img;
static uint16_t *s_bg_buf; /* SCREEN_W x SCREEN_H RGB565, rebuilt when the style changes */
static saver_style_t s_bg_style = SAVER_STYLE_COUNT;

static void cage_ensure(saver_style_t style, const saver_palette_t *pal) {
    if (!s_cage_buf) s_cage_buf = lv_malloc(CAGE_W * CAGE_H * sizeof(uint16_t));
    if (s_cage_buf && s_cage_style != style) {
        s_cage_style = style;
        radial_fill((uint16_t *)s_cage_buf, CAGE_W, CAGE_H, CAGE_W / 2.0f, CAGE_H / 2.0f,
                    CAGE_W * CAGE_RX_PCT / PCT_DIV, CAGE_H * CAGE_RY_PCT / PCT_DIV, CAGE_STOP_PCT / PCT_DIV,
                    pal->cage_in, pal->cage_out);
        image_dsc_rgb565(&s_cage_img, (uint16_t *)s_cage_buf, CAGE_W, CAGE_H);
    }
}

static void background_ensure(saver_style_t style, const saver_palette_t *pal) {
    if (!s_bg_buf) s_bg_buf = lv_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t));
    if (!s_bg_buf || s_bg_style == style) return;
    s_bg_style = style;
    bool orange = style == SAVER_STYLE_ORANGE;
    radial_fill(s_bg_buf, SCREEN_W, SCREEN_H,
                SCREEN_W * (orange ? BG_ORANGE_CX_PCT : BG_THEME_CX_PCT) / PCT_DIV,
                SCREEN_H * (orange ? BG_ORANGE_CY_PCT : BG_THEME_CY_PCT) / PCT_DIV,
                SCREEN_W * (orange ? BG_ORANGE_RX_PCT : BG_THEME_RX_PCT) / PCT_DIV,
                SCREEN_H * (orange ? BG_ORANGE_RY_PCT : BG_THEME_RY_PCT) / PCT_DIV,
                (orange ? BG_ORANGE_STOP_PCT : BG_THEME_STOP_PCT) / PCT_DIV, pal->bg_in, pal->bg_out);
    image_dsc_rgb565(&s_bg_img, s_bg_buf, SCREEN_W, SCREEN_H);
}

/* Digits are drawn once into ARGB images and shown as images. LVGL turns every
 * polyline segment into its own draw task and its task list handling grows
 * quadratically, so ~9000 live line segments froze the P4's LVGL task for
 * 20-36 s per frame (board run 2026-10-15). */
#define DIGIT_COUNT 10
#define DIGIT_PAD 16 /* room for the widest glow stroke around the window */
#define DIGIT_IMG_W (SAVER_DIGIT_WINDOW_W + 2 * DIGIT_PAD)
#define DIGIT_IMG_H (SAVER_DIGIT_WINDOW_H + 2 * DIGIT_PAD)
#define ARGB8888_BYTES 4

static lv_image_dsc_t s_digit_img[DIGIT_COUNT], s_ghost_img;
static uint8_t *s_digit_buf[DIGIT_COUNT], *s_ghost_buf;
static saver_style_t s_digit_style = SAVER_STYLE_COUNT;

static void polyline(lv_layer_t *layer, const lv_point_precise_t *pts, uint16_t n, lv_color_t c, int width, lv_opa_t opa) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = width;
    d.opa = opa;
    d.round_start = 1;
    d.round_end = 1;
    for (uint16_t i = 1; i < n; i++) {
        d.p1.x = pts[i - 1].x + DIGIT_PAD;
        d.p1.y = pts[i - 1].y + DIGIT_PAD;
        d.p2.x = pts[i].x + DIGIT_PAD;
        d.p2.y = pts[i].y + DIGIT_PAD;
        lv_draw_line(layer, &d);
    }
}

/* Allocates a transparent ARGB8888 image and lets `fill` draw into it. */
typedef void (*image_fill_cb_t)(lv_layer_t *layer, int arg, const saver_palette_t *pal);

static uint8_t *image_render(lv_image_dsc_t *dsc, image_fill_cb_t fill, int arg, const saver_palette_t *pal) {
    uint8_t *buf = lv_malloc(DIGIT_IMG_W * DIGIT_IMG_H * ARGB8888_BYTES);
    if (!buf) return NULL;
    lv_obj_t *canvas = lv_canvas_create(lv_layer_top());
    lv_canvas_set_buffer(canvas, buf, DIGIT_IMG_W, DIGIT_IMG_H, LV_COLOR_FORMAT_ARGB8888);
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    fill(&layer, arg, pal);
    lv_canvas_finish_layer(canvas, &layer);
    lv_obj_delete(canvas);
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.w = DIGIT_IMG_W;
    dsc->header.h = DIGIT_IMG_H;
    dsc->header.stride = DIGIT_IMG_W * ARGB8888_BYTES;
    dsc->data_size = DIGIT_IMG_W * DIGIT_IMG_H * ARGB8888_BYTES;
    dsc->data = buf;
    return buf;
}

static void fill_ghosts(lv_layer_t *layer, int arg, const saver_palette_t *pal) {
    LV_UNUSED(arg);
    for (int d = 0; d < DIGIT_COUNT; d++)
        polyline(layer, k_saver_digits[d].pts, k_saver_digits[d].count, pal->ghost, GHOST_WIDTH, GHOST_OPA);
}

/* One glow layer drawn fully opaque: overlapping round caps of a translucent
 * stroke would add up to a string of beads, so the layer's opacity is applied
 * afterwards, when the whole layer is laid over the digit image. */
static void fill_stroke(lv_layer_t *layer, int packed, const saver_palette_t *pal) {
    int digit = packed / LIT_LAYERS, i = packed % LIT_LAYERS;
    polyline(layer, k_saver_digits[digit].pts, k_saver_digits[digit].count,
             pal->glow[k_layers[i].color_idx], k_layers[i].width, LV_OPA_COVER);
}

static lv_image_dsc_t s_stroke_img[LIT_LAYERS];
static uint8_t *s_stroke_buf[LIT_LAYERS]; /* freed once the digit image is finished */

static void fill_lit(lv_layer_t *layer, int digit, const saver_palette_t *pal) {
    for (int i = 0; i < LIT_LAYERS; i++) {
        s_stroke_buf[i] = image_render(&s_stroke_img[i], fill_stroke, digit * LIT_LAYERS + i, pal);
        if (!s_stroke_buf[i]) continue;
        lv_draw_image_dsc_t id;
        lv_draw_image_dsc_init(&id);
        id.src = &s_stroke_img[i];
        id.opa = k_layers[i].opa;
        lv_area_t area = { 0, 0, DIGIT_IMG_W - 1, DIGIT_IMG_H - 1 };
        lv_draw_image(layer, &id, &area);
    }
}

static saver_palette_t s_palette; /* of the style currently built; lit digits render lazily with it */

/* Called when the style changes: drops every cached digit, redraws the ghosts. */
static void digits_reset(saver_style_t style, const saver_palette_t *pal) {
    if (s_digit_style == style) return;
    s_digit_style = style;
    for (int d = 0; d < DIGIT_COUNT; d++) {
        if (s_digit_buf[d]) lv_free(s_digit_buf[d]);
        s_digit_buf[d] = NULL;
    }
    if (s_ghost_buf) lv_free(s_ghost_buf);
    s_ghost_buf = image_render(&s_ghost_img, fill_ghosts, 0, pal);
}

typedef struct {
    lv_obj_t *lit;
    uint8_t shown;
} tube_t;

static struct {
    lv_obj_t *overlay, *clock_block, *date_row, *date_lbl, *sep, *temp_lbl;
    tube_t tubes[SAVER_DIGIT_COUNT];
    saver_style_t style;
    weather_saver_dismiss_cb_t on_dismiss;
    bool visible;
} sv;

/* Everything on the overlay is decoration: lv_obj_create() defaults to
 * clickable, which would swallow the press before it reached the overlay. */
static lv_obj_t *deco(lv_obj_t *o) {
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c, lv_opa_t opa, int radius) {
    lv_obj_t *o = deco(lv_obj_create(parent));
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

static void glow(lv_obj_t *o, lv_color_t c, int width, lv_opa_t opa) {
    lv_obj_set_style_shadow_color(o, c, 0);
    lv_obj_set_style_shadow_width(o, width, 0);
    lv_obj_set_style_shadow_opa(o, opa, 0);
}

static void build_tube(lv_obj_t *row, int x, tube_t *t, const saver_palette_t *pal) {
    lv_obj_t *tube = rect(row, x, 0, TUBE_W, TUBE_H, pal->bg, LV_OPA_TRANSP, 0);

    lv_obj_t *glass = rect(tube, 0, 0, TUBE_W, TUBE_H - GLASS_BOTTOM_INSET, lv_color_white(), GLASS_FILL_OPA, TUBE_W / 2);
    lv_obj_set_style_border_width(glass, 1, 0);
    lv_obj_set_style_border_color(glass, lv_color_hex(0xD2E1DC), 0);
    lv_obj_set_style_border_opa(glass, GLASS_BORDER_OPA, 0);
    rect(tube, TUBE_W / 2 - CAP_W / 2, -5, CAP_W, CAP_H, lv_color_hex(0xDCE6E4), LV_OPA_20, 4);

    if (s_cage_buf) {
        lv_obj_t *backdrop = deco(lv_image_create(tube));
        lv_image_set_src(backdrop, &s_cage_img);
        lv_obj_set_pos(backdrop, CAGE_X, CAGE_Y);
    }
    /* Rings and glow: 0 0 0 2px ring, 0 0 0 3px outer ring, 0 0 40px accent 25 %. */
    lv_obj_t *cage = rect(tube, CAGE_X, CAGE_Y, CAGE_W, CAGE_H, pal->cage_in, LV_OPA_TRANSP, 2);
    lv_obj_set_style_border_width(cage, 2, 0);
    lv_obj_set_style_border_color(cage, pal->cage_ring, 0);
    lv_obj_set_style_outline_width(cage, CAGE_RING_OUTER_PX, 0);
    lv_obj_set_style_outline_color(cage, pal->cage_ring_outer, 0);
    glow(cage, pal->cage_glow, CAGE_GLOW_WIDTH, CAGE_GLOW_OPA);

    if (s_ghost_buf) {
        lv_obj_t *ghosts = deco(lv_image_create(tube));
        lv_image_set_src(ghosts, &s_ghost_img);
        lv_obj_set_pos(ghosts, WINDOW_X - DIGIT_PAD, WINDOW_Y - DIGIT_PAD);
    }
    t->lit = deco(lv_image_create(tube));
    lv_obj_set_pos(t->lit, WINDOW_X - DIGIT_PAD, WINDOW_Y - DIGIT_PAD);
    lv_obj_add_flag(t->lit, LV_OBJ_FLAG_HIDDEN);
    t->shown = DIGIT_NONE;

    mesh_ensure();
    if (s_mesh_buf) {
        lv_obj_t *mesh = deco(lv_image_create(tube));
        lv_image_set_src(mesh, &s_mesh_img);
        lv_obj_set_pos(mesh, CAGE_X, CAGE_Y);
        lv_obj_set_style_image_recolor(mesh, pal->mesh, 0);
        lv_obj_set_style_image_recolor_opa(mesh, LV_OPA_COVER, 0);
        lv_obj_set_style_image_opa(mesh, MESH_OPA, 0);
    }

    rect(tube, PIN_X, PIN_TOP_Y, PIN_SIZE, PIN_SIZE, pal->pin1, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    rect(tube, PIN_X, PIN_BOTTOM_Y, PIN_SIZE, PIN_SIZE, pal->pin2, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    lv_obj_t *base = rect(tube, BASE_X, TUBE_H - GLASS_BOTTOM_INSET - BASE_H, TUBE_W - 2 * BASE_X, BASE_H,
                          lv_color_hex(0xD2E1DC), LV_OPA_10, 8);
    LV_UNUSED(base);
    rect(tube, SHINE_L_X, SHINE_L_Y, SHINE_L_W, SHINE_L_H, lv_color_white(), LV_OPA_10, 3);
    rect(tube, SHINE_R_X, SHINE_R_Y, SHINE_R_W, SHINE_R_H, lv_color_white(), LV_OPA_10, 2);
}

/* The design's four stacked box-shadows around each dot: tight bright ring out to a wide dim bloom. */
static const struct { uint8_t width; uint8_t spread; lv_opa_t opa; } k_dot_halo[DOT_HALO_LAYERS] = {
    { 3, 1, LV_OPA_COVER }, { 10, 3, 217 }, { 22, 8, 140 }, { 40, 14, 77 },
};

/* Two dots, vertically centred on the tube row (the design centres the
 * column including its bottom margin, so it sits slightly above the middle). */
static void build_dots(lv_obj_t *row, int x, const saver_palette_t *pal) {
    int column_h = 2 * DOT_SIZE + DOT_GAP + DOT_BOTTOM_MARGIN;
    int top = (TUBE_H - column_h) / 2;
    for (int i = 0; i < 2; i++) {
        int y = top + i * (DOT_SIZE + DOT_GAP);
        for (int h = DOT_HALO_LAYERS - 1; h >= 0; h--) {
            lv_obj_t *layer = rect(row, x, y, DOT_SIZE, DOT_SIZE, pal->dot, LV_OPA_COVER, LV_RADIUS_CIRCLE);
            glow(layer, pal->dot_halo[h], k_dot_halo[h].width, k_dot_halo[h].opa);
            lv_obj_set_style_shadow_spread(layer, k_dot_halo[h].spread, 0);
        }
        int inset = (DOT_SIZE - DOT_CORE_SIZE) / 2;
        rect(row, x + inset, y + inset, DOT_CORE_SIZE, DOT_CORE_SIZE, pal->dot_core, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    }
}

/* Gradient descriptors must outlive the style that points at them. */
static lv_grad_dsc_t s_bar_base, s_bar_shade, s_bar_grain_fine, s_bar_grain_long;

static lv_obj_t *bar_layer(lv_obj_t *bar) {
    lv_obj_t *l = rect(bar, 0, 0, CLOCK_W, BAR_H, lv_color_black(), LV_OPA_TRANSP, BAR_RADIUS);
    return l;
}

static void layer_gradient(lv_obj_t *l, const lv_grad_dsc_t *g) {
    lv_obj_set_style_bg_grad(l, g, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
}

/* Mounting bar across the tube bases, above the tubes. The design stacks a
 * four-stop metal gradient, a light top edge and a dark bottom edge (and, in
 * the orange style, a wood grain), with a deep drop shadow. */
static void build_bar(lv_obj_t *parent, saver_style_t style) {
    bool orange = style == SAVER_STYLE_ORANGE;
    static const uint8_t base_fracs[BAR_STOPS] = { 0, 77, 158, 255 };
    const lv_color_t theme_cols[BAR_STOPS] = { lv_color_hex(0x3F424D), lv_color_hex(0x595D6C),
                                               lv_color_hex(0x3F424D), lv_color_hex(0x595D6C) };
    const lv_color_t orange_cols[BAR_STOPS] = { lv_color_hex(0x391F13), lv_color_hex(0x452818),
                                                lv_color_hex(0x321B12), lv_color_hex(0x3F2415) };

    if (orange) {
        /* 0 2px 0 oklch(.2 .03 40): a dark lower lip under the bar. */
        rect(parent, 0, BAR_Y + BAR_EDGE_DROP_PX, CLOCK_W, BAR_H, lv_color_hex(0x22110B), LV_OPA_COVER, BAR_RADIUS);
    }

    lv_obj_t *bar = bar_layer(parent);
    lv_obj_set_pos(bar, 0, BAR_Y);
    lv_obj_set_style_clip_corner(bar, true, 0);
    lv_obj_set_style_shadow_color(bar, lv_color_black(), 0);
    lv_obj_set_style_shadow_offset_y(bar, orange ? BAR_SHADOW_OFFSET_ORANGE : BAR_SHADOW_OFFSET_THEME, 0);
    lv_obj_set_style_shadow_width(bar, orange ? BAR_SHADOW_WIDTH_ORANGE : BAR_SHADOW_WIDTH_THEME, 0);
    lv_obj_set_style_shadow_opa(bar, orange ? BAR_SHADOW_OPA_ORANGE : BAR_SHADOW_OPA_THEME, 0);
    if (!orange) { /* --shadow-lg also draws a 1px neutral-500 ring */
        lv_obj_set_style_outline_width(bar, 1, 0);
        lv_obj_set_style_outline_color(bar, lv_color_hex(0x9397AB), 0);
    }

    lv_grad_horizontal_init(&s_bar_base);
    lv_grad_init_stops(&s_bar_base, orange ? orange_cols : theme_cols, NULL, base_fracs, BAR_STOPS);
    layer_gradient(bar, &s_bar_base);

    if (orange) {
        /* Wood grain, from the design's two repeating-linear-gradients. */
        static const uint8_t fine_fracs[BAR_GRAIN_STOPS] = { 0, 61, 67, 73, 133, 140, 146, 231, 243, 255 };
        const lv_color_t dark = lv_color_black(), light = lv_color_hex(0xFFC896);
        const lv_color_t fine_cols[BAR_GRAIN_STOPS] = { dark, dark, dark, dark, dark, light, dark, dark, dark, dark };
        const lv_opa_t fine_opa[BAR_GRAIN_STOPS] = { 0, 0, 46, 0, 0, 13, 0, 0, 31, 0 };
        /* 178.6deg is almost straight down: a 1px sideways drift per period. */
        lv_grad_linear_init(&s_bar_grain_fine, 0, 0, 1, BAR_GRAIN_PERIOD_PX, LV_GRAD_EXTEND_REPEAT);
        lv_grad_init_stops(&s_bar_grain_fine, fine_cols, fine_opa, fine_fracs, BAR_GRAIN_STOPS);
        lv_obj_t *fine = bar_layer(bar);
        layer_gradient(fine, &s_bar_grain_fine);

        static const uint8_t long_fracs[BAR_STOPS] = { 0, 108, 170, 255 };
        const lv_color_t long_cols[BAR_STOPS] = { dark, dark, dark, dark };
        const lv_opa_t long_opa[BAR_STOPS] = { 0, 0, 20, 0 };
        lv_grad_linear_init(&s_bar_grain_long, 0, 0, BAR_GRAIN_LONG_PERIOD_PX, 0, LV_GRAD_EXTEND_REPEAT);
        lv_grad_init_stops(&s_bar_grain_long, long_cols, long_opa, long_fracs, BAR_STOPS);
        lv_obj_t *broad = bar_layer(bar);
        layer_gradient(broad, &s_bar_grain_long);
    }

    /* Light top edge fading to clear, darkening again towards the bottom. */
    const lv_color_t hi = orange ? lv_color_hex(0xFFDCB4) : lv_color_hex(0xCFD3E5);
    const lv_color_t shade_cols[BAR_STOPS] = { hi, hi, lv_color_black(), lv_color_black() };
    const lv_opa_t shade_opa[BAR_STOPS] = { orange ? 36 : 46, 0, 0, BAR_SHADE_OPA };
    const uint8_t shade_fracs[BAR_STOPS] = { 0, BAR_LIGHT_EDGE_FRAC, BAR_SHADE_START_FRAC, 255 };
    lv_grad_vertical_init(&s_bar_shade);
    lv_grad_init_stops(&s_bar_shade, shade_cols, shade_opa, shade_fracs, BAR_STOPS);
    layer_gradient(bar_layer(bar), &s_bar_shade);

    /* inset 0 1px 0 <highlight> */
    rect(bar, BAR_RADIUS, 0, CLOCK_W - 2 * BAR_RADIUS, 1, orange ? lv_color_hex(0xFFD7AA) : lv_color_hex(0xD2CEFD),
         orange ? BAR_INNER_LIGHT_OPA_ORANGE : BAR_INNER_LIGHT_OPA_THEME, 0);
}

static void on_press(lv_event_t *e) {
    LV_UNUSED(e);
    if (sv.on_dismiss) sv.on_dismiss();
}

void weather_saver_build(saver_style_t style, weather_saver_dismiss_cb_t on_dismiss) {
    bool was_visible = sv.visible;
    if (sv.overlay) lv_obj_delete(sv.overlay);
    memset(&sv, 0, sizeof sv);
    sv.style = style;
    sv.on_dismiss = on_dismiss;
    saver_palette_t pal = (style == SAVER_STYLE_ORANGE) ? palette_orange() : palette_theme();
    s_palette = pal;
    digits_reset(style, &pal);
    cage_ensure(style, &pal);
    background_ensure(style, &pal);

    sv.overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(sv.overlay);
    lv_obj_set_size(sv.overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(sv.overlay, 0, 0);
    lv_obj_set_style_bg_color(sv.overlay, pal.bg_out, 0);
    lv_obj_set_style_bg_opa(sv.overlay, LV_OPA_COVER, 0);
    if (s_bg_buf) {
        lv_obj_t *backdrop = deco(lv_image_create(sv.overlay));
        lv_image_set_src(backdrop, &s_bg_img);
        lv_obj_set_pos(backdrop, 0, 0);
    }
    lv_obj_remove_flag(sv.overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sv.overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sv.overlay, on_press, LV_EVENT_PRESSED, NULL);

    sv.clock_block = rect(sv.overlay, 0, 0, CLOCK_W, CLOCK_BLOCK_H, pal.bg, LV_OPA_TRANSP, 0);
    int x = CLOCK_PAD_X, tube = 0;
    for (int group = 0; group < 3; group++) {
        for (int k = 0; k < 2; k++, tube++) build_tube(sv.clock_block, x + k * (TUBE_W + TUBE_GAP), &sv.tubes[tube], &pal);
        x += GROUP_W;
        if (group < 2) {
            x += ROW_GAP;
            build_dots(sv.clock_block, x, &pal);
            x += DOT_SIZE + ROW_GAP;
        }
    }
    build_bar(sv.clock_block, style);

    sv.date_row = rect(sv.overlay, 0, 0, SCREEN_W, DATE_ROW_H, pal.bg, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(sv.date_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sv.date_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sv.date_row, DATE_GAP, 0);
    sv.date_lbl = deco(lv_label_create(sv.date_row));
    lv_obj_set_style_text_font(sv.date_lbl, FONT_22, 0);
    lv_obj_set_style_text_color(sv.date_lbl, pal.text, 0);
    sv.sep = rect(sv.date_row, 0, 0, SEPARATOR_SIZE, SEPARATOR_SIZE, pal.text, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    glow(sv.sep, pal.text_glow, 8, LV_OPA_60);
    sv.temp_lbl = deco(lv_label_create(sv.date_row));
    lv_obj_set_style_text_font(sv.temp_lbl, FONT_22, 0);
    lv_obj_set_style_text_color(sv.temp_lbl, pal.text, 0);

    if (!was_visible) lv_obj_add_flag(sv.overlay, LV_OBJ_FLAG_HIDDEN);
    sv.visible = was_visible;
}

void weather_saver_show(void) {
    if (!sv.overlay) return;
    for (int i = 0; i < SAVER_DIGIT_COUNT; i++) sv.tubes[i].shown = DIGIT_NONE; /* force a full redraw */
    lv_obj_remove_flag(sv.overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(sv.overlay);
    sv.visible = true;
}

void weather_saver_hide(void) {
    if (!sv.overlay) return;
    lv_obj_add_flag(sv.overlay, LV_OBJ_FLAG_HIDDEN);
    sv.visible = false;
}

bool weather_saver_is_visible(void) { return sv.visible; }

static void set_digit(tube_t *t, uint8_t d) {
    if (t->shown == d) return;
    t->shown = d;
    if (!s_digit_buf[d]) {
        s_digit_buf[d] = image_render(&s_digit_img[d], fill_lit, d, &s_palette);
        for (int i = 0; i < LIT_LAYERS; i++) {
            if (s_stroke_buf[i]) lv_free(s_stroke_buf[i]);
            s_stroke_buf[i] = NULL;
        }
    }
    if (!s_digit_buf[d]) return;
    lv_image_set_src(t->lit, &s_digit_img[d]);
    lv_obj_remove_flag(t->lit, LV_OBJ_FLAG_HIDDEN);
}

void weather_saver_update(const struct tm *lt, bool use_12h, int64_t unix_seconds,
                          const char *date, const char *temp) {
    if (!sv.overlay || !sv.visible || !lt) return;
    uint8_t digits[SAVER_DIGIT_COUNT];
    screensaver_split_digits(lt->tm_hour, lt->tm_min, lt->tm_sec, use_12h, digits);
    for (int i = 0; i < SAVER_DIGIT_COUNT; i++) set_digit(&sv.tubes[i], digits[i]);

    lv_label_set_text(sv.date_lbl, date ? date : "");
    bool has_temp = temp && temp[0];
    lv_label_set_text(sv.temp_lbl, has_temp ? temp : "");
    if (has_temp) { lv_obj_remove_flag(sv.sep, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(sv.temp_lbl, LV_OBJ_FLAG_HIDDEN); }
    else          { lv_obj_add_flag(sv.sep, LV_OBJ_FLAG_HIDDEN);    lv_obj_add_flag(sv.temp_lbl, LV_OBJ_FLAG_HIDDEN); }

    /* The clock sits centred in the space the date row leaves free; the date
     * row swaps between bottom and top every ten minutes. */
    saver_drift_t d = screensaver_drift(unix_seconds);
    int free_h = SCREEN_H - DATE_ROW_H;
    int clock_y = (d.date_above ? DATE_ROW_H : 0) + (free_h - TUBE_H) / 2; /* the bar overhangs the tubes, like in the design */
    int date_y = d.date_above ? 0 : SCREEN_H - DATE_ROW_H;
    lv_obj_set_pos(sv.clock_block, (SCREEN_W - CLOCK_W) / 2 + d.dx, clock_y + d.dy);
    lv_obj_set_pos(sv.date_row, d.dx, date_y + d.dy);
}
