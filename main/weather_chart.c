#include "weather_chart.h"
#include <math.h>
#include <stdio.h>
#include "ui_fonts.h"

/* Nocturne tokens, same values as styles.css (LVGL has no CSS variables). */
#define C_SURFACE     lv_color_hex(0x232532)
#define C_TEXT_MUTED  lv_color_hex(0x9397AB)
#define C_ACCENT      lv_color_hex(0x9184D9)
#define C_ACCENT_600  lv_color_hex(0x796CBF)
#define C_NEUTRAL_800 lv_color_hex(0x3F424D)

#define TEMP_PANEL_GROW 3
#define PRECIP_PANEL_H  26
#define AXIS_H          12
#define BAR_MAX_W       24
#define BAR_GAP          2   /* the surface gap that separates touching bars */
#define LINE_W           2
#define DOT_R            5   /* >= 8px diameter marker */

typedef struct {
    lv_obj_t *temp_panel, *precip_row, *axis_row;
    lv_obj_t *line;
    lv_point_precise_t *pts;      /* lv_line does not copy its points */
    lv_obj_t *hi_lbl, *lo_lbl, *end_dot;
} chart_t;

static void chart_free_cb(lv_event_t *e) {
    chart_t *c = lv_obj_get_user_data(lv_event_get_target_obj(e));
    if (!c) return;
    lv_free(c->pts);
    lv_free(c);
}

lv_obj_t *weather_chart_create(lv_obj_t *parent, int32_t w, int32_t h) {
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, w, h);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    chart_t *c = lv_malloc(sizeof(chart_t));
    if (!c) return root;
    lv_memset(c, 0, sizeof(chart_t));
    c->pts = lv_malloc(sizeof(lv_point_precise_t) * WEATHER_UI_CHART_POINTS);
    lv_obj_set_user_data(root, c);
    lv_obj_add_event_cb(root, chart_free_cb, LV_EVENT_DELETE, NULL);

    /* Temperature panel — one series, one scale. */
    c->temp_panel = lv_obj_create(root);
    lv_obj_remove_style_all(c->temp_panel);
    lv_obj_set_width(c->temp_panel, LV_PCT(100));
    lv_obj_set_flex_grow(c->temp_panel, TEMP_PANEL_GROW);
    lv_obj_remove_flag(c->temp_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Baseline: hairline, solid, one step off the surface — recessive by design. */
    lv_obj_t *rule = lv_obj_create(c->temp_panel);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, LV_PCT(100), 1);
    lv_obj_align(rule, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(rule, C_NEUTRAL_800, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    c->line = lv_line_create(c->temp_panel);
    lv_obj_set_style_line_color(c->line, C_ACCENT, 0);
    lv_obj_set_style_line_width(c->line, LINE_W, 0);
    lv_obj_set_style_line_rounded(c->line, true, 0);

    /* End marker with a surface ring so it stays legible over the line. */
    c->end_dot = lv_obj_create(c->temp_panel);
    lv_obj_remove_style_all(c->end_dot);
    lv_obj_set_size(c->end_dot, DOT_R * 2, DOT_R * 2);
    lv_obj_set_style_radius(c->end_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c->end_dot, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(c->end_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c->end_dot, C_SURFACE, 0);
    lv_obj_set_style_border_width(c->end_dot, 2, 0);
    lv_obj_add_flag(c->end_dot, LV_OBJ_FLAG_HIDDEN);

    /* Only the two extremes get a direct label; the rest is the reader's to hover
     * or read off the day cards. A number on all 24 points would go unread. */
    c->hi_lbl = lv_label_create(c->temp_panel);
    lv_obj_set_style_text_color(c->hi_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(c->hi_lbl, FONT_10, 0);
    lv_label_set_text(c->hi_lbl, "");
    c->lo_lbl = lv_label_create(c->temp_panel);
    lv_obj_set_style_text_color(c->lo_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(c->lo_lbl, FONT_10, 0);
    lv_label_set_text(c->lo_lbl, "");

    /* Precipitation panel — its own scale, its own frame. */
    /* No flex on either of these: their children are placed at the same x as the
     * line's points, so the temperature and precipitation panels stay in register. */
    c->precip_row = lv_obj_create(root);
    lv_obj_remove_style_all(c->precip_row);
    lv_obj_set_size(c->precip_row, LV_PCT(100), PRECIP_PANEL_H);
    lv_obj_remove_flag(c->precip_row, LV_OBJ_FLAG_SCROLLABLE);

    c->axis_row = lv_obj_create(root);
    lv_obj_remove_style_all(c->axis_row);
    lv_obj_set_size(c->axis_row, LV_PCT(100), AXIS_H);
    lv_obj_remove_flag(c->axis_row, LV_OBJ_FLAG_SCROLLABLE);

    return root;
}

static int32_t disp_temp(float c_deg, bool f) {
    return (int32_t)lroundf(f ? c_deg * 9.0f / 5.0f + 32.0f : c_deg);
}

void weather_chart_set_data(lv_obj_t *chart, const weather_hourly_t *h, bool fahrenheit) {
    chart_t *c = lv_obj_get_user_data(chart);
    if (!c || !c->pts) return;

    lv_obj_clean(c->precip_row);
    lv_obj_clean(c->axis_row);

    int n = (h && h->count > 0) ? h->count : 0;
    if (n > WEATHER_UI_CHART_POINTS) n = WEATHER_UI_CHART_POINTS;
    if (n < 2) {
        lv_obj_add_flag(c->line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(c->end_dot, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(c->hi_lbl, "");
        lv_label_set_text(c->lo_lbl, "");
        return;
    }
    lv_obj_remove_flag(c->line, LV_OBJ_FLAG_HIDDEN);

    /* The panels are laid out by flex, so their size is only known after a refresh. */
    lv_obj_update_layout(chart);
    int32_t w = lv_obj_get_width(c->temp_panel);
    int32_t ph = lv_obj_get_height(c->temp_panel);
    if (w <= 0 || ph <= 0) return;

    const int32_t pad_top = 12, pad_bot = 4;   /* room for the hi label and the baseline */
    int32_t plot_h = ph - pad_top - pad_bot;
    if (plot_h < 8) plot_h = 8;

    float tmin = h->temp_c[0], tmax = h->temp_c[0];
    int imin = 0, imax = 0;
    for (int i = 1; i < n; i++) {
        if (h->temp_c[i] < tmin) { tmin = h->temp_c[i]; imin = i; }
        if (h->temp_c[i] > tmax) { tmax = h->temp_c[i]; imax = i; }
    }
    float range = tmax - tmin;
    if (range < 0.5f) range = 0.5f;   /* a flat day must not become a divide-by-zero */

    float step = (n > 1) ? (float)(w - DOT_R * 2) / (float)(n - 1) : 0.0f;
    for (int i = 0; i < n; i++) {
        c->pts[i].x = (lv_value_precise_t)(DOT_R + i * step);
        c->pts[i].y = (lv_value_precise_t)(pad_top + plot_h - ((h->temp_c[i] - tmin) / range) * plot_h);
    }
    lv_line_set_points(c->line, c->pts, n);

    lv_obj_remove_flag(c->end_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(c->end_dot, (int32_t)c->pts[n - 1].x - DOT_R, (int32_t)c->pts[n - 1].y - DOT_R);

    char buf[16];
    snprintf(buf, sizeof buf, "%d\xC2\xB0", (int)disp_temp(tmax, fahrenheit));
    lv_label_set_text(c->hi_lbl, buf);
    snprintf(buf, sizeof buf, "%d\xC2\xB0", (int)disp_temp(tmin, fahrenheit));
    lv_label_set_text(c->lo_lbl, buf);
    /* Park each label beside its own extreme, clamped so it can't run off either edge. */
    lv_obj_update_layout(c->hi_lbl);
    int32_t hw = lv_obj_get_width(c->hi_lbl), lw = lv_obj_get_width(c->lo_lbl);
    int32_t hx = (int32_t)c->pts[imax].x - hw / 2;
    int32_t lx = (int32_t)c->pts[imin].x - lw / 2;
    if (hx < 0) hx = 0;
    if (hx > w - hw) hx = w - hw;
    if (lx < 0) lx = 0;
    if (lx > w - lw) lx = w - lw;
    lv_obj_set_pos(c->hi_lbl, hx, (int32_t)c->pts[imax].y - 14);
    lv_obj_set_pos(c->lo_lbl, lx, (int32_t)c->pts[imin].y + 2);

    /* Precipitation bars: one scale, grown from a single baseline. */
    float pmax = 0.0f;
    for (int i = 0; i < n; i++) if (h->precip_mm[i] > pmax) pmax = h->precip_mm[i];
    if (pmax < 0.2f) pmax = 0.2f;

    /* One bar per slot, capped, with a 2px surface gap to its neighbour. */
    int32_t bar_w = (int32_t)step - BAR_GAP;
    if (bar_w > BAR_MAX_W) bar_w = BAR_MAX_W;
    if (bar_w < 1) bar_w = 1;

    /* The points sit DOT_R in from either edge, so anything wider than 2*DOT_R
     * centred on the first or last point hangs over the panel and gets clipped.
     * Same clamp the two temperature labels get above. */
    int32_t precip_w = lv_obj_get_width(c->precip_row);

    for (int i = 0; i < n; i++) {
        int32_t bh = (int32_t)((h->precip_mm[i] / pmax) * (PRECIP_PANEL_H - 2));
        if (h->precip_mm[i] > 0.0f && bh < 2) bh = 2;   /* a real trace must stay visible */
        if (bh <= 0) continue;

        int32_t bx = (int32_t)c->pts[i].x - bar_w / 2;
        if (bx < 0) bx = 0;
        if (bx > precip_w - bar_w) bx = precip_w - bar_w;

        lv_obj_t *bar = lv_obj_create(c->precip_row);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, bar_w, bh);
        lv_obj_set_pos(bar, bx, PRECIP_PANEL_H - bh);
        lv_obj_set_style_bg_color(bar, C_ACCENT_600, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    }

    /* Hour ticks every 6 h — enough to orient, few enough to stay quiet. */
    int32_t axis_w = lv_obj_get_width(c->axis_row);
    for (int i = 0; i < n; i++) {
        if (h->hour[i] % 6 != 0) continue;
        lv_obj_t *t = lv_label_create(c->axis_row);
        lv_label_set_text_fmt(t, "%02d", h->hour[i]);
        lv_obj_set_style_text_color(t, C_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(t, FONT_10, 0);
        /* Measure rather than assume a half-width: a fixed 8px offset left the 00
         * tick at x = -3, where the panel edge ate its first digit, and mis-centred
         * every other tick by the difference. */
        lv_obj_update_layout(t);
        int32_t tw = lv_obj_get_width(t);
        int32_t tx = (int32_t)c->pts[i].x - tw / 2;
        if (tx < 0) tx = 0;
        if (tx > axis_w - tw) tx = axis_w - tw;
        lv_obj_set_pos(t, tx, 0);
    }
}
