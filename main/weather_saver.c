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
    lv_color_t bg, bar, cage, cage_ring, pin1, pin2, ghost, mesh;
    lv_color_t glow[4];       /* g1..g4 */
    lv_color_t dot_core, dot, dot_halo[DOT_HALO_LAYERS], text, text_glow;
} saver_palette_t;

/* "App theme": Nocturne tokens (design/src/nocturne/styles.css). */
static saver_palette_t palette_theme(void) {
    return (saver_palette_t){
        .bg = lv_color_hex(0x161826), .bar = lv_color_hex(0x3F424D), .cage = lv_color_hex(0x353350),
        .cage_ring = lv_color_hex(0x595D6C), .pin1 = lv_color_hex(0x595D6C), .pin2 = lv_color_hex(0x3F424D),
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
        .bg = lv_color_hex(0x18120F), .bar = lv_color_hex(0x452818), .cage = lv_color_hex(0x501A0A),
        .cage_ring = lv_color_hex(0x37322E), .pin1 = lv_color_hex(0x322D29), .pin2 = lv_color_hex(0x28231F),
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

typedef struct {
    lv_obj_t *lit[LIT_LAYERS];
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

static lv_obj_t *line(lv_obj_t *parent, lv_color_t c, int width, lv_opa_t opa) {
    lv_obj_t *l = deco(lv_line_create(parent));
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_size(l, SAVER_DIGIT_WINDOW_W, SAVER_DIGIT_WINDOW_H);
    lv_obj_set_style_line_color(l, c, 0);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_opa(l, opa, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

static void build_tube(lv_obj_t *row, int x, tube_t *t, const saver_palette_t *pal) {
    lv_obj_t *tube = rect(row, x, 0, TUBE_W, TUBE_H, pal->bg, LV_OPA_TRANSP, 0);

    lv_obj_t *glass = rect(tube, 0, 0, TUBE_W, TUBE_H - GLASS_BOTTOM_INSET, lv_color_white(), GLASS_FILL_OPA, TUBE_W / 2);
    lv_obj_set_style_border_width(glass, 1, 0);
    lv_obj_set_style_border_color(glass, lv_color_hex(0xD2E1DC), 0);
    lv_obj_set_style_border_opa(glass, GLASS_BORDER_OPA, 0);
    rect(tube, TUBE_W / 2 - CAP_W / 2, -5, CAP_W, CAP_H, lv_color_hex(0xDCE6E4), LV_OPA_20, 4);

    lv_obj_t *cage = rect(tube, CAGE_X, CAGE_Y, CAGE_W, CAGE_H, pal->cage, LV_OPA_COVER, 2);
    lv_obj_set_style_border_width(cage, 2, 0);
    lv_obj_set_style_border_color(cage, pal->cage_ring, 0);
    glow(cage, pal->glow[1], 40, LV_OPA_20);

    lv_obj_t *win = rect(tube, WINDOW_X, WINDOW_Y, SAVER_DIGIT_WINDOW_W, SAVER_DIGIT_WINDOW_H, pal->bg, LV_OPA_TRANSP, 0);
    for (int d = 0; d < 10; d++) {
        lv_obj_t *g = line(win, pal->ghost, GHOST_WIDTH, GHOST_OPA);
        lv_line_set_points(g, k_saver_digits[d].pts, k_saver_digits[d].count);
    }
    for (int i = 0; i < LIT_LAYERS; i++) {
        t->lit[i] = line(win, pal->glow[k_layers[i].color_idx], k_layers[i].width, k_layers[i].opa);
        lv_obj_add_flag(t->lit[i], LV_OBJ_FLAG_HIDDEN);
    }
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

    sv.overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(sv.overlay);
    lv_obj_set_size(sv.overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(sv.overlay, 0, 0);
    lv_obj_set_style_bg_color(sv.overlay, pal.bg, 0);
    lv_obj_set_style_bg_opa(sv.overlay, LV_OPA_COVER, 0);
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
    /* Mounting bar across the tube bases, above the tubes. */
    lv_obj_t *bar = rect(sv.clock_block, 0, BAR_Y, CLOCK_W, BAR_H, pal.bar, LV_OPA_COVER, BAR_RADIUS);
    glow(bar, lv_color_black(), 30, LV_OPA_50);

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
    for (int i = 0; i < LIT_LAYERS; i++) {
        lv_line_set_points(t->lit[i], k_saver_digits[d].pts, k_saver_digits[d].count);
        lv_obj_remove_flag(t->lit[i], LV_OBJ_FLAG_HIDDEN);
    }
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
