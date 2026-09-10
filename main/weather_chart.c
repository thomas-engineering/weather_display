#include "weather_chart.h"
#include <math.h>
#include <stdio.h>
#include "ui_fonts.h"

/* Nocturne tokens, same values as styles.css (LVGL has no CSS variables). */
#define C_TEXT_MUTED  lv_color_hex(0x9397AB)
#define C_ACCENT      lv_color_hex(0x9184D9)
#define C_ACCENT_700  lv_color_hex(0x5D5294)
#define C_DIVIDER     lv_color_hex(0x3A3D4A)

/* Pixel padding around the plot, reserved for the two axes' tick labels and
 * the x-axis hour labels — mirrors the design's buildDayChart() PAD_L/R/T/B
 * (Weather App.dc.html), just in fixed pixels instead of a fixed 480x108/150
 * SVG viewBox, since LVGL draws at the panel's actual size. */
#define PAD_L 22
/* FIX: the design's PAD_R=30 is a fraction of its 480-unit SVG viewBox, scaled
 * down to whatever the panel actually renders at — its physical margin ends
 * up well under FONT_10's width. Drawn at fixed LVGL pixels, 26 wasn't enough
 * for precip labels like "0.5mm"/"12mm", which overlapped the right axis
 * line; wide enough for the longest realistic label plus a visible gap. */
#define PAD_R 40
#define PAD_T 8
#define PAD_B 14

#define LEGEND_GAP    6   /* between the two legend rows */
#define LEGEND_MARK   8   /* legend dot/square side, matches the design's 8x8 */
#define LINE_W        2
#define BAR_GAP_FRAC  0.4f /* bar takes 60% of its slot, 40% gap — design's barW = barSlot*0.55, close enough at FONT_10 scale */
#define BAR_MAX_W     20
#define PRECIP_HEIGHT_FRAC 0.85f /* bars/the precip axis only use the top 85% of the plot height, same as the design */

typedef struct {
    lv_obj_t *legend_temp_lbl, *legend_precip_lbl;
    lv_obj_t *plot;
    lv_obj_t *line;
    lv_point_precise_t *pts;      /* lv_line does not copy its points */
} chart_t;

static void chart_free_cb(lv_event_t *e) {
    chart_t *c = lv_obj_get_user_data(lv_event_get_target_obj(e));
    if (!c) return;
    lv_free(c->pts);
    lv_free(c);
}

static lv_obj_t *legend_row_create(lv_obj_t *parent, lv_color_t mark_color, bool round_mark) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 5, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mark = lv_obj_create(row);
    lv_obj_remove_style_all(mark);
    lv_obj_set_size(mark, LEGEND_MARK, LEGEND_MARK);
    lv_obj_set_style_radius(mark, round_mark ? LV_RADIUS_CIRCLE : 2, 0);
    lv_obj_set_style_bg_color(mark, mark_color, 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_remove_flag(mark, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_set_style_text_color(lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl, FONT_10, 0);
    lv_label_set_text(lbl, "");
    return lbl;
}

lv_obj_t *weather_chart_create(lv_obj_t *parent, int32_t w, int32_t h) {
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, w, h);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(root, 10, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    chart_t *c = lv_malloc(sizeof(chart_t));
    if (!c) return root;
    lv_memset(c, 0, sizeof(chart_t));
    c->pts = lv_malloc(sizeof(lv_point_precise_t) * WEATHER_UI_CHART_POINTS);
    lv_obj_set_user_data(root, c);
    lv_obj_add_event_cb(root, chart_free_cb, LV_EVENT_DELETE, NULL);

    /* Legend, to the left of the plot: a temperature dot and a precipitation
     * square, each followed by its caption — matches the design's two-row
     * legend column left of the SVG (Weather App.dc.html, current.chart /
     * selectedDay.chart block). */
    lv_obj_t *legend_col = lv_obj_create(root);
    lv_obj_remove_style_all(legend_col);
    lv_obj_set_size(legend_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(legend_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(legend_col, LEGEND_GAP, 0);
    lv_obj_remove_flag(legend_col, LV_OBJ_FLAG_SCROLLABLE);
    c->legend_temp_lbl = legend_row_create(legend_col, C_ACCENT, true);
    c->legend_precip_lbl = legend_row_create(legend_col, C_ACCENT_700, false);

    /* The plot itself: free-form (no flex), every child absolutely
     * positioned — axis lines, temperature line, precipitation bars, and
     * both axes' tick labels all rebuilt together in weather_chart_set_data()
     * since their positions all depend on that call's min/max. */
    c->plot = lv_obj_create(root);
    lv_obj_remove_style_all(c->plot);
    lv_obj_set_height(c->plot, LV_PCT(100));
    lv_obj_set_flex_grow(c->plot, 1);
    lv_obj_remove_flag(c->plot, LV_OBJ_FLAG_SCROLLABLE);

    return root;
}

static int32_t disp_temp(float c_deg, bool f) {
    return (int32_t)lroundf(f ? c_deg * 9.0f / 5.0f + 32.0f : c_deg);
}

/* Right-axis (precipitation) tick text: one decimal below 1mm, whole mm
 * above — same rule as the design's buildDayChart() tempTicks/precipTicks. */
static void fmt_precip_tick(char *buf, size_t cap, float mm) {
    if (mm < 1.0f) snprintf(buf, cap, "%.1fmm", (double)mm);
    else           snprintf(buf, cap, "%.0fmm", (double)mm);
}

void weather_chart_set_data(lv_obj_t *chart, const weather_hourly_t *h, bool fahrenheit,
                             const char *temp_label, const char *precip_label) {
    chart_t *c = lv_obj_get_user_data(chart);
    if (!c || !c->pts) return;

    lv_label_set_text(c->legend_temp_lbl, temp_label ? temp_label : "");
    lv_label_set_text(c->legend_precip_lbl, precip_label ? precip_label : "");

    lv_obj_clean(c->plot);
    c->line = NULL;

    int n = (h && h->count > 0) ? h->count : 0;
    if (n > WEATHER_UI_CHART_POINTS) n = WEATHER_UI_CHART_POINTS;
    if (n < 2) return;

    lv_obj_update_layout(chart);
    int32_t w = lv_obj_get_width(c->plot);
    int32_t full_h = lv_obj_get_height(c->plot);
    if (w <= 0 || full_h <= 0) return;

    int32_t chart_w = w - PAD_L - PAD_R;
    int32_t chart_h = full_h - PAD_T - PAD_B;
    if (chart_w < 4 || chart_h < 4) return;

    /* Axis lines, left and right edges of the plot proper — the tick labels
     * live in the PAD_L/PAD_R margins outside them. */
    lv_obj_t *axis_l = lv_obj_create(c->plot);
    lv_obj_remove_style_all(axis_l);
    lv_obj_set_size(axis_l, 1, chart_h);
    lv_obj_set_pos(axis_l, PAD_L, PAD_T);
    lv_obj_set_style_bg_color(axis_l, C_DIVIDER, 0);
    lv_obj_set_style_bg_opa(axis_l, LV_OPA_COVER, 0);
    /* FIX: see the CLICKABLE comment on the precip bars below — same pitfall. */
    lv_obj_remove_flag(axis_l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(axis_l, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *axis_r = lv_obj_create(c->plot);
    lv_obj_remove_style_all(axis_r);
    lv_obj_set_size(axis_r, 1, chart_h);
    lv_obj_set_pos(axis_r, PAD_L + chart_w, PAD_T);
    lv_obj_set_style_bg_color(axis_r, C_DIVIDER, 0);
    lv_obj_set_style_bg_opa(axis_r, LV_OPA_COVER, 0);
    lv_obj_remove_flag(axis_r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(axis_r, LV_OBJ_FLAG_SCROLLABLE);

    float tmin = h->temp_c[0], tmax = h->temp_c[0];
    for (int i = 1; i < n; i++) {
        if (h->temp_c[i] < tmin) tmin = h->temp_c[i];
        if (h->temp_c[i] > tmax) tmax = h->temp_c[i];
    }
    float trange = tmax - tmin;
    if (trange < 0.5f) trange = 0.5f;   /* a flat day must not become a divide-by-zero */

    float pmax = 0.0f;
    for (int i = 0; i < n; i++) if (h->precip_mm[i] > pmax) pmax = h->precip_mm[i];
    /* FIX: matches the design's `Math.max(1, ...precs)` (buildDayChart() in
     * Weather App.dc.html). A 0.2mm floor here made a 0.1mm trace fill ~42%
     * of the bar height ((0.1/0.2)*85%) instead of the design's ~8.5%
     * ((0.1/1)*85%) — every low-precipitation day rendered with visually
     * oversized bars. */
    if (pmax < 1.0f) pmax = 1.0f;

    /* Precipitation bars, drawn first so the temperature line sits on top —
     * same stacking as the design's <rect> bars followed by its <polyline>. */
    float step = (float)chart_w / (float)(n - 1);
    float bar_slot = (float)chart_w / (float)n;
    int32_t bar_w = (int32_t)(bar_slot * (1.0f - BAR_GAP_FRAC));
    if (bar_w > BAR_MAX_W) bar_w = BAR_MAX_W;
    if (bar_w < 1) bar_w = 1;

    for (int i = 0; i < n; i++) {
        int32_t bh = (int32_t)((h->precip_mm[i] / pmax) * (chart_h * PRECIP_HEIGHT_FRAC));
        if (h->precip_mm[i] > 0.0f && bh < 2) bh = 2;   /* a real trace must stay visible */
        if (bh <= 0) continue;

        int32_t bx = PAD_L + (int32_t)(i * bar_slot + (bar_slot - bar_w) / 2.0f);
        lv_obj_t *bar = lv_obj_create(c->plot);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, bar_w, bh);
        lv_obj_set_pos(bar, bx, PAD_T + chart_h - bh);
        lv_obj_set_style_bg_color(bar, C_ACCENT_700, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        /* FIX: lv_obj_create() defaults to CLICKABLE (same pitfall as the
         * weather-icon dots — see weather_icons.c's dot()). The bars are
         * rebuilt from scratch on every weather_chart_set_data() call, so a
         * tap on one landed on this decorative, unhandled-click object
         * instead of falling through to the day-detail sheet's own
         * close-on-tap handler: the panel just sat there. */
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    }

    /* Temperature line. */
    for (int i = 0; i < n; i++) {
        c->pts[i].x = (lv_value_precise_t)(PAD_L + i * step);
        c->pts[i].y = (lv_value_precise_t)(PAD_T + chart_h - ((h->temp_c[i] - tmin) / trange) * chart_h);
    }
    c->line = lv_line_create(c->plot);
    lv_obj_set_style_line_color(c->line, C_ACCENT, 0);
    lv_obj_set_style_line_width(c->line, LINE_W, 0);
    lv_obj_set_style_line_rounded(c->line, true, 0);
    lv_line_set_points(c->line, c->pts, n);

    /* Left axis: temperature, 3 ticks (max/mid/min) — same f=[0,0.5,1] scheme
     * as the design, just written directly since LVGL positions in pixels. */
    for (int k = 0; k < 3; k++) {
        float f = (float)k / 2.0f;
        float val = tmin + f * trange;
        int32_t y = PAD_T + (int32_t)(chart_h - f * chart_h);
        char buf[8];
        snprintf(buf, sizeof buf, "%d\xC2\xB0", (int)disp_temp(val, fahrenheit));
        lv_obj_t *t = lv_label_create(c->plot);
        lv_label_set_text(t, buf);
        lv_obj_set_style_text_color(t, C_ACCENT, 0);
        lv_obj_set_style_text_font(t, FONT_10, 0);
        lv_obj_update_layout(t);
        lv_obj_set_pos(t, 0, y - lv_obj_get_height(t) / 2);
    }

    /* Right axis: precipitation, same 3-tick scheme, scaled to the same 85%
     * height the bars use so a tick's y actually lines up with that value's
     * bar height. */
    for (int k = 0; k < 3; k++) {
        float f = (float)k / 2.0f;
        float val = f * pmax;
        int32_t y = PAD_T + (int32_t)(chart_h - f * (chart_h * PRECIP_HEIGHT_FRAC));
        char buf[16];
        fmt_precip_tick(buf, sizeof buf, val);
        lv_obj_t *t = lv_label_create(c->plot);
        lv_label_set_text(t, buf);
        lv_obj_set_style_text_color(t, C_ACCENT_700, 0);
        lv_obj_set_style_text_font(t, FONT_10, 0);
        lv_obj_update_layout(t);
        lv_obj_set_pos(t, w - lv_obj_get_width(t), y - lv_obj_get_height(t) / 2);
    }

    /* X-axis: hour labels every 3 points (up to 8 for a full 24h day),
     * matching the design's `for (i = 0; i < n; i += 3)`. */
    for (int i = 0; i < n; i += 3) {
        lv_obj_t *t = lv_label_create(c->plot);
        lv_label_set_text_fmt(t, "%02d", h->hour[i]);
        lv_obj_set_style_text_color(t, C_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(t, FONT_10, 0);
        lv_obj_update_layout(t);
        int32_t tw = lv_obj_get_width(t);
        int32_t tx = PAD_L + (int32_t)(i * step) - tw / 2;
        if (tx < 0) tx = 0;
        if (tx > w - tw) tx = w - tw;
        lv_obj_set_pos(t, tx, full_h - lv_obj_get_height(t));
    }
}
