/* Weather UI — LVGL v9.x port of the "Weather App" design.
 *
 * Scope / assumptions (see README_LVGL.md for the full list):
 *  - Display 1024x600, landscape, resistive/capacitive touch, no physical keyboard
 *    (city search uses LVGL's built-in on-screen keyboard widget).
 *  - This file owns layout, styling and interaction only. Networking (Open-Meteo
 *    forecast + geocoding calls) and date/weekday formatting are the app's job —
 *    call the weather_ui_set_* functions with already-fetched, already-localized
 *    data. Hook points are the weather_ui_set_callbacks() function pointers.
 *  - Colors are the Nocturne design system's token values, hardcoded (LVGL has no
 *    CSS variables). Fonts fall back to the built-in Montserrat set; swap for a
 *    real Inter .c font (lv_font_conv) for a pixel-accurate match — see README.
 */

#include "weather_ui.h"
#include "weather_icons.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- Nocturne color tokens -------------------------------------------- */
#define C_BG          lv_color_hex(0x161826)
#define C_SURFACE     lv_color_hex(0x232532)
#define C_TEXT        lv_color_hex(0xE9E9ED)
#define C_TEXT_MUTED  lv_color_hex(0x9397AB)
#define C_ACCENT      lv_color_hex(0x9184D9)
#define C_ACCENT_100  lv_color_hex(0xF5F4FF)
#define C_ACCENT_700  lv_color_hex(0x5D5294)
#define C_ACCENT_800  lv_color_hex(0x423A6A)
#define C_ACCENT_900  lv_color_hex(0x2B2741)
#define C_NEUTRAL_900 lv_color_hex(0x292B31)
#define C_DIVIDER     lv_color_hex(0x3A3D4A)
#define R_MD 8
#define R_LG 14

typedef struct {
    lv_obj_t *root, *header_location_lbl, *header_time_lbl, *header_date_lbl;
    lv_obj_t *current_card, *current_icon, *current_cond_tag, *current_temp_lbl, *current_feel_lbl;
    lv_obj_t *stat_humidity_val, *stat_humidity_sub, *stat_humidity_pct, *stat_feel_val, *stat_wind_val, *stat_precip_val;
    lv_obj_t *forecast_strip;
    lv_obj_t *day_cards[WEATHER_UI_DAYS];
    lv_obj_t *day_icons[WEATHER_UI_DAYS];
    lv_obj_t *day_hi_lbl[WEATHER_UI_DAYS], *day_lo_lbl[WEATHER_UI_DAYS], *day_precip_lbl[WEATHER_UI_DAYS];

    lv_obj_t *error_bar, *error_lbl, *loading_lbl;

    lv_obj_t *settings_backdrop, *settings_panel;
    lv_obj_t *seg_lang[4], *seg_temp[2], *seg_wind[3], *seg_time[2];

    lv_obj_t *search_backdrop, *search_ta, *search_kb, *search_results, *search_status_lbl;

    lv_obj_t *detail_backdrop, *detail_panel, *detail_icon, *detail_day_lbl, *detail_hilo_lbl, *detail_cond_lbl;
    lv_obj_t *detail_feel_lbl, *detail_precip_lbl, *detail_wind_lbl, *detail_chart, *detail_precip_chart;
    lv_chart_series_t *detail_temp_series, *detail_precip_series;

    lv_obj_t *current_chart, *current_precip_chart;
    lv_chart_series_t *current_temp_series, *current_precip_series;

    lv_obj_t *header_net_dot, *header_net_lbl, *header_stale_tag;

    lv_obj_t *wifi_backdrop, *wifi_panel;
    lv_obj_t *wifi_list_view, *wifi_list_box, *wifi_scan_btn, *wifi_scan_status_lbl;
    lv_obj_t *wifi_manual_view, *wifi_manual_ta, *wifi_manual_kb;
    lv_obj_t *wifi_pass_view, *wifi_pass_ta, *wifi_pass_kb, *wifi_pass_title_lbl, *wifi_pass_show_btn, *wifi_connect_btn, *wifi_connect_btn_lbl;
    char wifi_pending_ssid[33];
    char wifi_pass_plain[65];
    bool wifi_pass_visible;
    bool wifi_pending_secured;
    wx_wifi_network_t wifi_networks[32];
    int wifi_networks_count;

    weather_current_t cur;
    weather_day_t days[WEATHER_UI_DAYS];
    weather_hourly_t hourly_today;
    weather_hourly_t hourly_days[WEATHER_UI_DAYS];
    bool hourly_today_valid;
    bool hourly_days_valid[WEATHER_UI_DAYS];
    bool has_data;
    weather_lang_t lang;
    wx_temp_unit_t temp_unit;
    wx_wind_unit_t wind_unit;
    wx_time_fmt_t time_fmt;

    weather_ui_search_cb_t on_search;
    weather_ui_select_city_cb_t on_select_city;
    weather_ui_refresh_cb_t on_refresh;
    weather_ui_settings_changed_cb_t on_settings_changed;
    weather_ui_wifi_scan_cb_t on_wifi_scan;
    weather_ui_wifi_connect_cb_t on_wifi_connect;
} weather_ui_t;

static weather_ui_t ui;

/* ---- formatting helpers (mirrors the web version's unit math) --------- */

static int temp_disp(float c) { return (ui.temp_unit == WX_UNIT_F) ? (int)lroundf(c * 9.0f / 5.0f + 32.0f) : (int)lroundf(c); }
static char temp_unit_ch(void) { return ui.temp_unit == WX_UNIT_F ? 'F' : 'C'; }

static float wind_disp(float kmh) {
    if (ui.wind_unit == WX_WIND_MPH) return kmh * 0.621371f;
    if (ui.wind_unit == WX_WIND_MS) return kmh / 3.6f;
    return kmh;
}
static const char *wind_unit_str(void) { return ui.wind_unit == WX_WIND_MPH ? "mph" : ui.wind_unit == WX_WIND_MS ? "m/s" : "km/h"; }

static const char *humidity_cat(int pct) {
    const weather_strings_t *s = &weather_strings[ui.lang];
    if (pct < 30) return s->humid_dry;
    if (pct < 60) return s->humid_comfortable;
    if (pct < 80) return s->humid_humid;
    return s->humid_very_humid;
}

static int wmo_bucket(int code) {
    if (code == 0) return 0;
    if (code == 1) return 1;
    if (code == 2) return 2;
    if (code == 3) return 3;
    if (code == 45 || code == 48) return 4;
    if (code == 71 || code == 73 || code == 75 || code == 77 || code == 85 || code == 86) return 6;
    if (code == 95 || code == 96 || code == 99) return 7;
    return 5; /* drizzle/rain/showers */
}

/* ---- small "segmented control" helper (mirrors the .seg CSS component) - */

typedef void (*seg_cb_t)(int index, void *user);

typedef struct { seg_cb_t cb; void *user; int count; } seg_ctx_t;

static void seg_click_cb(lv_event_t *e) {
    lv_obj_t **group = (lv_obj_t **)lv_event_get_user_data(e);
    lv_obj_t *clicked = lv_event_get_target(e);
    seg_ctx_t *ctx = (seg_ctx_t *)lv_obj_get_user_data(clicked);
    for (int i = 0; i < ctx->count; i++) lv_obj_set_style_bg_opa(group[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(clicked, LV_OPA_20, 0);
    int idx = -1;
    for (int i = 0; i < ctx->count; i++) if (group[i] == clicked) idx = i;
    if (ctx->cb) ctx->cb(idx, ctx->user);
}

/* Builds a row of `count` toggle buttons acting as one radio group; `out` receives
 * the button objects (caller-owned array, size >= count) so selection can be set
 * programmatically later. */
static lv_obj_t *seg_create(lv_obj_t *parent, const char *const labels[], int count, int selected, seg_cb_t cb, void *user, lv_obj_t **out) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, C_DIVIDER, 0);
    lv_obj_set_style_radius(row, R_MD, 0);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 34);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    static seg_ctx_t ctxs[16]; /* enough for every seg control this UI creates */
    static int ctx_n = 0;

    for (int i = 0; i < count; i++) {
        lv_obj_t *btn = lv_obj_create(row);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_PCT(100));
        lv_obj_set_style_pad_hor(btn, 12, 0);
        lv_obj_set_style_bg_color(btn, C_ACCENT, 0);
        lv_obj_set_style_bg_opa(btn, i == selected ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_side(btn, i == 0 ? LV_BORDER_SIDE_NONE : LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(btn, C_DIVIDER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_color(lbl, C_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);

        seg_ctx_t *ctx = &ctxs[ctx_n++];
        ctx->cb = cb; ctx->user = user; ctx->count = count;
        lv_obj_set_user_data(btn, ctx);
        out[i] = btn;
        lv_obj_add_event_cb(btn, seg_click_cb, LV_EVENT_CLICKED, out);
    }
    return row;
}

static lv_obj_t *field_wrap(lv_obj_t *parent, const char *label_text) {
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_width(wrap, LV_PCT(100));
    lv_obj_set_height(wrap, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wrap, 5, 0);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = lv_label_create(wrap);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    return wrap;
}

/* ---- header ------------------------------------------------------------ */

static void refresh_click_cb(lv_event_t *e) { LV_UNUSED(e); if (ui.on_refresh) ui.on_refresh(); }
static void open_search_cb(lv_event_t *e);
static void open_settings_cb(lv_event_t *e);

static lv_obj_t *icon_btn_create(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 36, 36);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, C_DIVIDER, 0);
    lv_obj_set_style_radius(btn, R_MD, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_color(lbl, C_TEXT, 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static void build_header(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 64);
    lv_obj_set_style_pad_all(row, 16, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *loc_btn = lv_obj_create(row);
    lv_obj_remove_style_all(loc_btn);
    lv_obj_set_size(loc_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_flag(loc_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(loc_btn, LV_OBJ_FLAG_SCROLLABLE);
    ui.header_location_lbl = lv_label_create(loc_btn);
    lv_label_set_text(ui.header_location_lbl, LV_SYMBOL_GPS " --");
    lv_obj_set_style_text_color(ui.header_location_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.header_location_lbl, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(loc_btn, open_search_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *right = lv_obj_create(row);
    lv_obj_remove_style_all(right);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 16, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *time_wrap = lv_obj_create(right);
    lv_obj_remove_style_all(time_wrap);
    lv_obj_set_size(time_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(time_wrap, 2, 0);
    lv_obj_clear_flag(time_wrap, LV_OBJ_FLAG_SCROLLABLE);
    ui.header_time_lbl = lv_label_create(time_wrap);
    lv_obj_set_style_text_color(ui.header_time_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.header_time_lbl, &lv_font_montserrat_20, 0);
    ui.header_date_lbl = lv_label_create(time_wrap);
    lv_obj_set_style_text_color(ui.header_date_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ui.header_date_lbl, &lv_font_montserrat_12, 0);

    icon_btn_create(right, LV_SYMBOL_REFRESH, refresh_click_cb);
    icon_btn_create(right, LV_SYMBOL_SETTINGS, open_settings_cb);

    lv_obj_t *net_wrap = lv_obj_create(right);
    lv_obj_remove_style_all(net_wrap);
    lv_obj_set_size(net_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(net_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(net_wrap, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(net_wrap, 8, 0);
    lv_obj_clear_flag(net_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(net_wrap, 0); /* place before time/buttons, i.e. leftmost of the right group */

    ui.header_stale_tag = lv_label_create(net_wrap);
    lv_label_set_text(ui.header_stale_tag, weather_strings[ui.lang].data_outdated);
    lv_obj_set_style_text_color(ui.header_stale_tag, C_ACCENT_100, 0);
    lv_obj_set_style_bg_color(ui.header_stale_tag, C_ACCENT_800, 0);
    lv_obj_set_style_bg_opa(ui.header_stale_tag, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.header_stale_tag, 6, 0);
    lv_obj_set_style_pad_hor(ui.header_stale_tag, 8, 0);
    lv_obj_set_style_pad_ver(ui.header_stale_tag, 3, 0);
    lv_obj_set_style_text_font(ui.header_stale_tag, &lv_font_montserrat_10, 0);
    lv_obj_add_flag(ui.header_stale_tag, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *net_status = lv_obj_create(net_wrap);
    lv_obj_remove_style_all(net_status);
    lv_obj_set_size(net_status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(net_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(net_status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(net_status, 6, 0);
    lv_obj_clear_flag(net_status, LV_OBJ_FLAG_SCROLLABLE);
    ui.header_net_dot = lv_obj_create(net_status);
    lv_obj_remove_style_all(ui.header_net_dot);
    lv_obj_set_size(ui.header_net_dot, 8, 8);
    lv_obj_set_style_radius(ui.header_net_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui.header_net_dot, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(ui.header_net_dot, LV_OPA_COVER, 0);
    ui.header_net_lbl = lv_label_create(net_status);
    lv_obj_set_style_text_color(ui.header_net_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ui.header_net_lbl, &lv_font_montserrat_12, 0);
    lv_label_set_text(ui.header_net_lbl, weather_strings[ui.lang].online);
}

/* ---- current weather card ---------------------------------------------- */

static lv_obj_t *stat_box_create(lv_obj_t *parent, const char *label, lv_obj_t **val_out, lv_obj_t **sub_out) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 104, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(box, C_NEUTRAL_900, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, R_MD, 0);
    lv_obj_set_style_pad_all(box, 8, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, label);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_color(lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_t *val = lv_label_create(box);
    lv_label_set_long_mode(val, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(val, LV_PCT(100));
    lv_obj_set_style_text_color(val, C_TEXT, 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
    *val_out = val;
    if (sub_out) {
        lv_obj_t *sub = lv_label_create(box);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(sub, LV_PCT(100));
        lv_obj_set_style_text_color(sub, C_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_11, 0);
        *sub_out = sub;
    }
    return box;
}

static void day_card_click_cb(lv_event_t *e);
static lv_obj_t *hourly_chart_create(lv_obj_t *parent, lv_coord_t h, lv_chart_series_t **temp_series_out, lv_obj_t **precip_chart_out, lv_chart_series_t **precip_series_out);
static void hourly_chart_apply(lv_obj_t *chart, lv_chart_series_t *temp_s, lv_obj_t *precip_chart, lv_chart_series_t *precip_s, const weather_hourly_t *hourly);

static void build_current_card(lv_obj_t *parent) {
    lv_obj_t *outer = lv_obj_create(parent);
    lv_obj_remove_style_all(outer);
    lv_obj_set_size(outer, LV_PCT(100), 250);
    lv_obj_set_style_bg_color(outer, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(outer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(outer, R_MD, 0);
    lv_obj_set_style_pad_all(outer, 16, 0);
    lv_obj_set_flex_flow(outer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(outer, 10, 0);
    lv_obj_clear_flag(outer, LV_OBJ_FLAG_SCROLLABLE);

    ui.current_card = lv_obj_create(outer);
    lv_obj_remove_style_all(ui.current_card);
    lv_obj_set_size(ui.current_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ui.current_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.current_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui.current_card, 20, 0);
    lv_obj_clear_flag(ui.current_card, LV_OBJ_FLAG_SCROLLABLE);

    ui.current_icon = weather_icon_create(ui.current_card, WX_ICON_CLOUD, 72, C_ACCENT);

    lv_obj_t *temp_wrap = lv_obj_create(ui.current_card);
    lv_obj_remove_style_all(temp_wrap);
    lv_obj_set_size(temp_wrap, 170, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(temp_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(temp_wrap, LV_OBJ_FLAG_SCROLLABLE);
    ui.current_cond_tag = lv_label_create(temp_wrap);
    lv_obj_set_style_text_color(ui.current_cond_tag, C_ACCENT_100, 0);
    lv_obj_set_style_bg_color(ui.current_cond_tag, C_ACCENT_800, 0);
    lv_obj_set_style_bg_opa(ui.current_cond_tag, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.current_cond_tag, 6, 0);
    lv_obj_set_style_pad_hor(ui.current_cond_tag, 8, 0);
    lv_obj_set_style_pad_ver(ui.current_cond_tag, 3, 0);
    lv_obj_set_style_text_font(ui.current_cond_tag, &lv_font_montserrat_12, 0);
    ui.current_temp_lbl = lv_label_create(temp_wrap);
    lv_obj_set_style_text_color(ui.current_temp_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.current_temp_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_pad_top(ui.current_temp_lbl, 6, 0);

    ui.current_feel_lbl = lv_label_create(ui.current_card);
    lv_label_set_long_mode(ui.current_feel_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(ui.current_feel_lbl, 1);
    lv_obj_set_style_text_color(ui.current_feel_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ui.current_feel_lbl, &lv_font_montserrat_14, 0);

    lv_obj_t *stats = lv_obj_create(ui.current_card);
    lv_obj_remove_style_all(stats);
    lv_obj_set_size(stats, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(stats, 8, 0);
    lv_obj_clear_flag(stats, LV_OBJ_FLAG_SCROLLABLE);
    stat_box_create(stats, weather_strings[ui.lang].humidity, &ui.stat_humidity_val, &ui.stat_humidity_sub);
    stat_box_create(stats, weather_strings[ui.lang].feels_like, &ui.stat_feel_val, NULL);
    stat_box_create(stats, weather_strings[ui.lang].wind, &ui.stat_wind_val, NULL);
    stat_box_create(stats, weather_strings[ui.lang].precip, &ui.stat_precip_val, NULL);

    ui.current_chart = hourly_chart_create(outer, 90, &ui.current_temp_series, &ui.current_precip_chart, &ui.current_precip_series);
}

/* ---- hourly temperature-line / precipitation-bar chart ------------------
 * One lv_chart with two Y axes (primary = temperature line, secondary =
 * precipitation bars) and hour-of-day labels on X, shared by the
 * current-weather card and each day's detail panel. */

static lv_obj_t *hourly_chart_create(lv_obj_t *parent, lv_coord_t h, lv_chart_series_t **temp_series_out, lv_obj_t **precip_chart_out, lv_chart_series_t **precip_series_out) {
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, LV_PCT(100), h);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

    /* precipitation drawn as real columns on its own bar-chart, stacked behind
     * the temperature line so it reads as a proper combo chart instead of the
     * fat-line hack (which showed as disconnected purple blobs, not bars). */
    lv_obj_t *pchart = lv_chart_create(wrap);
    lv_obj_set_size(pchart, LV_PCT(100), h);
    lv_obj_align(pchart, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_chart_set_type(pchart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(pchart, WEATHER_UI_CHART_POINTS);
    lv_obj_set_style_bg_opa(pchart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pchart, 0, 0);
    lv_chart_set_div_line_count(pchart, 0, 0);
    lv_chart_set_axis_tick(pchart, LV_CHART_AXIS_PRIMARY_X, 0, 0, 8, 3, false, 0);
    lv_chart_set_axis_tick(pchart, LV_CHART_AXIS_SECONDARY_Y, 0, 0, 3, 1, false, 0);
    lv_obj_set_style_pad_column(pchart, 3, 0);
    lv_chart_series_t *precip_s = lv_chart_add_series(pchart, C_ACCENT_700, LV_CHART_AXIS_SECONDARY_Y);
    lv_obj_set_style_bg_opa(pchart, LV_OPA_60, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pchart, C_ACCENT_700, LV_PART_ITEMS);
    lv_obj_set_style_radius(pchart, 2, LV_PART_ITEMS);
    lv_obj_clear_flag(pchart, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *chart = lv_chart_create(wrap);
    lv_obj_set_size(chart, LV_PCT(100), h);
    lv_obj_align(chart, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, WEATHER_UI_CHART_POINTS);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_line_color(chart, C_DIVIDER, LV_PART_MAIN);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR); /* hide point markers on the line */
    lv_chart_set_div_line_count(chart, 3, 0);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 4, 2, 8, 3, true, 40);
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 4, 2, 3, 1, true, 34);
    lv_obj_set_style_text_color(chart, C_ACCENT, LV_PART_TICKS | LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_text_color(chart, C_TEXT_MUTED, LV_PART_TICKS | LV_CHART_AXIS_PRIMARY_X);
    lv_obj_set_style_text_font(chart, &lv_font_montserrat_10, LV_PART_TICKS);

    lv_chart_series_t *temp_s = lv_chart_add_series(chart, C_ACCENT, LV_CHART_AXIS_PRIMARY_Y);

    *temp_series_out = temp_s;
    *precip_chart_out = pchart;
    *precip_series_out = precip_s;
    return chart;
}

static void hourly_chart_apply(lv_obj_t *chart, lv_chart_series_t *temp_s, lv_obj_t *precip_chart, lv_chart_series_t *precip_s, const weather_hourly_t *hourly) {
    if (!chart || !hourly) return;
    int n = hourly->count > WEATHER_UI_CHART_POINTS ? WEATHER_UI_CHART_POINTS : hourly->count;
    int pc = n > 0 ? n : 1;
    lv_chart_set_point_count(chart, pc);
    if (precip_chart) lv_chart_set_point_count(precip_chart, pc);
    for (int i = 0; i < n; i++) {
        lv_coord_t tv = (lv_coord_t)lroundf(ui.temp_unit == WX_UNIT_F ? (hourly->temp_c[i] * 9.0f / 5.0f + 32.0f) : hourly->temp_c[i]);
        lv_chart_set_value_by_id(chart, temp_s, i, tv);
        if (precip_chart && precip_s) lv_chart_set_value_by_id(precip_chart, precip_s, i, (lv_coord_t)lroundf(hourly->precip_mm[i] * 10)); /* 0.1mm resolution */
    }
    lv_chart_refresh(chart);
    if (precip_chart) lv_chart_refresh(precip_chart);
}

/* ---- forecast strip ------------------------------------------------------ */

static void build_forecast_strip(lv_obj_t *parent) {
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_remove_style_all(section);
    lv_obj_set_size(section, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(section, 1);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(section, 8, 0);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(section);
    lv_label_set_text(title, weather_strings[ui.lang].forecast);
    lv_obj_set_style_text_color(title, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);

    ui.forecast_strip = lv_obj_create(section);
    lv_obj_remove_style_all(ui.forecast_strip);
    lv_obj_set_size(ui.forecast_strip, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(ui.forecast_strip, 1);
    lv_obj_set_flex_flow(ui.forecast_strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(ui.forecast_strip, 12, 0);
    lv_obj_set_scroll_dir(ui.forecast_strip, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(ui.forecast_strip, LV_SCROLLBAR_MODE_AUTO);

    for (int i = 0; i < WEATHER_UI_DAYS; i++) {
        lv_obj_t *card = lv_obj_create(ui.forecast_strip);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, 120, LV_PCT(100));
        lv_obj_set_style_bg_color(card, C_SURFACE, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, C_DIVIDER, 0);
        lv_obj_set_style_radius(card, R_MD, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(card, 10, 0);
        lv_obj_set_style_pad_row(card, 6, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(card, (void *)(intptr_t)i);
        lv_obj_add_event_cb(card, day_card_click_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *day_lbl = lv_label_create(card);
        lv_obj_set_style_text_color(day_lbl, C_TEXT, 0);
        lv_obj_set_style_text_font(day_lbl, &lv_font_montserrat_14, 0);
        lv_obj_t *date_lbl = lv_label_create(card);
        lv_obj_set_style_text_color(date_lbl, C_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(date_lbl, &lv_font_montserrat_10, 0);

        ui.day_icons[i] = weather_icon_create(card, WX_ICON_CLOUD, 36, C_ACCENT);

        lv_obj_t *hilo = lv_obj_create(card);
        lv_obj_remove_style_all(hilo);
        lv_obj_set_size(hilo, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(hilo, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(hilo, 6, 0);
        lv_obj_clear_flag(hilo, LV_OBJ_FLAG_SCROLLABLE);
        ui.day_hi_lbl[i] = lv_label_create(hilo);
        lv_obj_set_style_text_color(ui.day_hi_lbl[i], C_TEXT, 0);
        lv_obj_set_style_text_font(ui.day_hi_lbl[i], &lv_font_montserrat_16, 0);
        ui.day_lo_lbl[i] = lv_label_create(hilo);
        lv_obj_set_style_text_color(ui.day_lo_lbl[i], C_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(ui.day_lo_lbl[i], &lv_font_montserrat_14, 0);

        ui.day_precip_lbl[i] = lv_label_create(card);
        lv_obj_set_style_text_color(ui.day_precip_lbl[i], C_ACCENT, 0);
        lv_obj_set_style_text_font(ui.day_precip_lbl[i], &lv_font_montserrat_10, 0);

        /* stash day/date labels where day_card_click_cb and render can find them again */
        lv_obj_set_user_data(day_lbl, (void *)(intptr_t)i);
        ui.day_cards[i] = card;
        lv_obj_add_flag(day_lbl, LV_OBJ_FLAG_USER_1);   /* marker, unused besides bookkeeping */
        lv_obj_set_style_pad_top(hilo, 2, 0);
        /* keep pointers reachable via card's children order: [0]=day,[1]=date,[2]=icon,[3]=hilo,[4]=precip */
    }
}

/* ---- render ------------------------------------------------------------- */

static void render_current(void) {
    if (!ui.has_data) return;
    lv_label_set_text_fmt(ui.header_location_lbl, LV_SYMBOL_GPS " %s%s%s", ui.cur.location_name,
                           ui.cur.location_country[0] ? ", " : "", ui.cur.location_country);
    lv_label_set_text(ui.header_time_lbl, ui.cur.time_str);
    lv_label_set_text(ui.header_date_lbl, ui.cur.date_str);

    int bucket = wmo_bucket(ui.cur.weather_code);
    lv_label_set_text(ui.current_cond_tag, weather_cond_text[ui.lang][bucket]);
    weather_icon_set_type(ui.current_icon, weather_icon_from_wmo(ui.cur.weather_code), 72, C_ACCENT);
    lv_label_set_text_fmt(ui.current_temp_lbl, "%d\xC2\xB0%c", temp_disp(ui.cur.temp_c), temp_unit_ch());
    lv_label_set_text(ui.current_feel_lbl, ui.cur.real_feel_text);

    lv_label_set_text(ui.stat_humidity_val, humidity_cat(ui.cur.humidity_pct));
    lv_label_set_text_fmt(ui.stat_feel_val, "%d\xC2\xB0%c", temp_disp(ui.cur.feels_like_c), temp_unit_ch());
    lv_label_set_text_fmt(ui.stat_wind_val, "%d %s", (int)lroundf(wind_disp(ui.cur.wind_kmh)), wind_unit_str());
    lv_label_set_text_fmt(ui.stat_precip_val, "%d%%", ui.cur.precip_pct);
    lv_label_set_text_fmt(ui.stat_humidity_sub, "%d%%", ui.cur.humidity_pct);
    if (ui.hourly_today_valid) hourly_chart_apply(ui.current_chart, ui.current_temp_series, ui.current_precip_chart, ui.current_precip_series, &ui.hourly_today);
}

static void render_days(void) {
    if (!ui.has_data) return;
    for (int i = 0; i < WEATHER_UI_DAYS; i++) {
        weather_day_t *d = &ui.days[i];
        lv_obj_t *day_lbl = lv_obj_get_child(ui.day_cards[i], 0);
        lv_obj_t *date_lbl = lv_obj_get_child(ui.day_cards[i], 1);
        lv_label_set_text(day_lbl, d->day_label);
        lv_label_set_text(date_lbl, d->date_label);
        weather_icon_set_type(ui.day_icons[i], weather_icon_from_wmo(d->weather_code), 36, C_ACCENT);
        lv_label_set_text_fmt(ui.day_hi_lbl[i], "%d\xC2\xB0", temp_disp(d->temp_max_c));
        lv_label_set_text_fmt(ui.day_lo_lbl[i], "%d\xC2\xB0", temp_disp(d->temp_min_c));
        lv_label_set_text_fmt(ui.day_precip_lbl[i], "%d%%", d->precip_pct);
    }
}

/* ---- day detail sliding panel ------------------------------------------- */

static void detail_close_cb(lv_event_t *e);

static void slide_anim_y_cb(void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, v); }
static void detail_hide_backdrop_cb(lv_anim_t *a) { LV_UNUSED(a); lv_obj_add_flag(ui.detail_backdrop, LV_OBJ_FLAG_HIDDEN); }

static void build_detail_panel(lv_obj_t *parent) {
    ui.detail_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.detail_backdrop);
    lv_obj_set_size(ui.detail_backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ui.detail_backdrop, C_NEUTRAL_900, 0);
    lv_obj_set_style_bg_opa(ui.detail_backdrop, LV_OPA_60, 0);
    lv_obj_add_flag(ui.detail_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.detail_backdrop, detail_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(ui.detail_backdrop, LV_OBJ_FLAG_HIDDEN);

    ui.detail_panel = lv_obj_create(ui.detail_backdrop);
    lv_obj_remove_style_all(ui.detail_panel);
    lv_obj_set_size(ui.detail_panel, LV_PCT(100), 420);
    lv_obj_align(ui.detail_panel, LV_ALIGN_BOTTOM_MID, 0, 420); /* parked off-screen below */
    lv_obj_set_style_bg_color(ui.detail_panel, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui.detail_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.detail_panel, R_LG, 0);
    lv_obj_set_style_pad_all(ui.detail_panel, 24, 0);
    lv_obj_set_flex_flow(ui.detail_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.detail_panel, 16, 0);
    lv_obj_add_flag(ui.detail_panel, LV_OBJ_FLAG_CLICKABLE); /* tapping the panel also closes it, per spec */
    lv_obj_add_event_cb(ui.detail_panel, detail_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *head = lv_obj_create(ui.detail_panel);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    ui.detail_day_lbl = lv_label_create(head);
    lv_obj_set_style_text_color(ui.detail_day_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.detail_day_lbl, &lv_font_montserrat_22, 0);

    lv_obj_t *body = lv_obj_create(ui.detail_panel);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(body, 20, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    ui.detail_icon = weather_icon_create(body, WX_ICON_CLOUD, 64, C_ACCENT);
    lv_obj_t *hilo_wrap = lv_obj_create(body);
    lv_obj_remove_style_all(hilo_wrap);
    lv_obj_set_size(hilo_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hilo_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(hilo_wrap, LV_OBJ_FLAG_SCROLLABLE);
    ui.detail_hilo_lbl = lv_label_create(hilo_wrap);
    lv_obj_set_style_text_color(ui.detail_hilo_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.detail_hilo_lbl, &lv_font_montserrat_36, 0);
    ui.detail_cond_lbl = lv_label_create(hilo_wrap);
    lv_obj_set_style_text_color(ui.detail_cond_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ui.detail_cond_lbl, &lv_font_montserrat_14, 0);

    lv_obj_t *stats = lv_obj_create(ui.detail_panel);
    lv_obj_remove_style_all(stats);
    lv_obj_set_size(stats, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(stats, 12, 0);
    lv_obj_clear_flag(stats, LV_OBJ_FLAG_SCROLLABLE);
    stat_box_create(stats, weather_strings[ui.lang].feels_like, &ui.detail_feel_lbl, NULL);
    stat_box_create(stats, weather_strings[ui.lang].precip, &ui.detail_precip_lbl, NULL);
    stat_box_create(stats, weather_strings[ui.lang].wind, &ui.detail_wind_lbl, NULL);

    ui.detail_chart = hourly_chart_create(ui.detail_panel, 100, &ui.detail_temp_series, &ui.detail_precip_chart, &ui.detail_precip_series);
}

static void detail_slide(bool open) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui.detail_panel);
    lv_anim_set_exec_cb(&a, slide_anim_y_cb);
    lv_anim_set_time(&a, 220);
    if (open) {
        lv_obj_clear_flag(ui.detail_backdrop, LV_OBJ_FLAG_HIDDEN);
        lv_anim_set_values(&a, 420, 0);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    } else {
        lv_anim_set_values(&a, 0, 420);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_set_completed_cb(&a, (lv_anim_completed_cb_t)NULL);
    }
    lv_anim_start(&a);
    if (!open) lv_anim_set_completed_cb(&a, detail_hide_backdrop_cb);
}

static void detail_close_cb(lv_event_t *e) { LV_UNUSED(e); detail_slide(false); }

static void day_card_click_cb(lv_event_t *e) {
    lv_obj_t *card = lv_event_get_target(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(card);
    if (i < 0 || i >= WEATHER_UI_DAYS) return;
    weather_day_t *d = &ui.days[i];
    char buf[48];
    lv_label_set_text_fmt(ui.detail_day_lbl, "%s\n%s", d->day_label, d->date_label);
    weather_icon_set_type(ui.detail_icon, weather_icon_from_wmo(d->weather_code), 64, C_ACCENT);
    snprintf(buf, sizeof buf, "%d\xC2\xB0 / %d\xC2\xB0", temp_disp(d->temp_max_c), temp_disp(d->temp_min_c));
    lv_label_set_text(ui.detail_hilo_lbl, buf);
    lv_label_set_text(ui.detail_cond_lbl, weather_cond_text[ui.lang][wmo_bucket(d->weather_code)]);
    lv_label_set_text_fmt(ui.detail_feel_lbl, "%d\xC2\xB0 / %d\xC2\xB0", temp_disp(d->feels_max_c), temp_disp(d->feels_min_c));
    lv_label_set_text_fmt(ui.detail_precip_lbl, "%d%%", d->precip_pct);
    lv_label_set_text_fmt(ui.detail_wind_lbl, "%d %s", (int)lroundf(wind_disp(d->wind_max_kmh)), wind_unit_str());
    if (ui.hourly_days_valid[i]) hourly_chart_apply(ui.detail_chart, ui.detail_temp_series, ui.detail_precip_chart, ui.detail_precip_series, &ui.hourly_days[i]);
    detail_slide(true);
}

/* ---- search overlay (native on-screen keyboard, no physical keys needed) - */

static void search_ta_event_cb(lv_event_t *e) {
    if (ui.on_search) ui.on_search(lv_textarea_get_text(ui.search_ta));
}

static void search_result_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (ui.on_select_city) ui.on_select_city(idx);
    lv_obj_add_flag(ui.search_backdrop, LV_OBJ_FLAG_HIDDEN);
}

static void search_cancel_cb(lv_event_t *e) { LV_UNUSED(e); lv_obj_add_flag(ui.search_backdrop, LV_OBJ_FLAG_HIDDEN); }

static void open_search_cb(lv_event_t *e) {
    LV_UNUSED(e);
    lv_textarea_set_text(ui.search_ta, "");
    lv_obj_clean(ui.search_results);
    lv_obj_clear_flag(ui.search_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(ui.search_ta, LV_STATE_FOCUSED);
}

static void build_search_overlay(lv_obj_t *parent) {
    ui.search_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.search_backdrop);
    lv_obj_set_size(ui.search_backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ui.search_backdrop, C_BG, 0);
    lv_obj_set_style_bg_opa(ui.search_backdrop, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ui.search_backdrop, 20, 0);
    lv_obj_set_flex_flow(ui.search_backdrop, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.search_backdrop, 10, 0);
    lv_obj_add_flag(ui.search_backdrop, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *top = lv_obj_create(ui.search_backdrop);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(top, 10, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    ui.search_ta = lv_textarea_create(top);
    lv_textarea_set_one_line(ui.search_ta, true);
    lv_textarea_set_placeholder_text(ui.search_ta, weather_strings[ui.lang].search_placeholder);
    lv_obj_set_flex_grow(ui.search_ta, 1);
    lv_obj_add_event_cb(ui.search_ta, search_ta_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *cancel = lv_obj_create(top);
    lv_obj_remove_style_all(cancel);
    lv_obj_set_size(cancel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(cancel, 8, 0);
    lv_obj_set_style_border_width(cancel, 1, 0);
    lv_obj_set_style_border_color(cancel, C_DIVIDER, 0);
    lv_obj_set_style_radius(cancel, R_MD, 0);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cancel, search_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel);
    lv_label_set_text(cancel_lbl, weather_strings[ui.lang].cancel);
    lv_obj_set_style_text_color(cancel_lbl, C_TEXT, 0);

    ui.search_status_lbl = lv_label_create(ui.search_backdrop);
    lv_obj_set_style_text_color(ui.search_status_lbl, C_TEXT_MUTED, 0);
    lv_label_set_text(ui.search_status_lbl, "");

    ui.search_results = lv_obj_create(ui.search_backdrop);
    lv_obj_remove_style_all(ui.search_results);
    lv_obj_set_size(ui.search_results, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ui.search_results, 1);
    lv_obj_set_flex_flow(ui.search_results, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.search_results, 6, 0);

    /* Built-in LVGL keyboard — this is what makes typing possible on a
     * touch-only device with no physical keys. It targets whichever textarea
     * currently has focus via lv_keyboard_set_textarea(). */
    ui.search_kb = lv_keyboard_create(ui.search_backdrop);
    lv_obj_set_size(ui.search_kb, LV_PCT(100), 220);
    lv_keyboard_set_textarea(ui.search_kb, ui.search_ta);
    lv_keyboard_set_mode(ui.search_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
}

void weather_ui_set_search_results(const char *const names[], const char *const subs[], int count) {
    lv_obj_clean(ui.search_results);
    lv_label_set_text(ui.search_status_lbl, count == 0 ? weather_strings[ui.lang].no_cities : "");
    for (int i = 0; i < count; i++) {
        lv_obj_t *row = lv_obj_create(ui.search_results);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, C_SURFACE, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, C_DIVIDER, 0);
        lv_obj_set_style_radius(row, R_MD, 0);
        lv_obj_set_style_pad_all(row, 12, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, search_result_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *n = lv_label_create(row);
        lv_label_set_text(n, names[i]);
        lv_obj_set_style_text_color(n, C_TEXT, 0);
        lv_obj_t *s = lv_label_create(row);
        lv_label_set_text(s, subs[i]);
        lv_obj_set_style_text_color(s, C_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(s, &lv_font_montserrat_12, 0);
    }
}

/* ---- settings panel ------------------------------------------------------ */

static void settings_close_cb(lv_event_t *e) { LV_UNUSED(e); lv_obj_add_flag(ui.settings_backdrop, LV_OBJ_FLAG_HIDDEN); }
static void open_settings_cb(lv_event_t *e) { LV_UNUSED(e); lv_obj_clear_flag(ui.settings_backdrop, LV_OBJ_FLAG_HIDDEN); }
static void wifi_show_view(lv_obj_t *view);

static void open_wifi_cb(lv_event_t *e) {
    LV_UNUSED(e);
    lv_obj_clear_flag(ui.wifi_backdrop, LV_OBJ_FLAG_HIDDEN);
    wifi_show_view(ui.wifi_list_view);
    lv_obj_clean(ui.wifi_list_box);
    ui.wifi_networks_count = 0;
    lv_label_set_text(ui.wifi_scan_status_lbl, "");
    if (ui.on_wifi_scan) {
        lv_label_set_text(ui.wifi_scan_status_lbl, weather_strings[ui.lang].scanning);
        ui.on_wifi_scan();
    }
}

static void notify_settings_changed(void) {
    render_current();
    render_days();
    if (ui.on_settings_changed) ui.on_settings_changed(ui.lang, ui.temp_unit, ui.wind_unit, ui.time_fmt);
}
static void on_seg_lang(int idx, void *user) { LV_UNUSED(user); weather_ui_set_language((weather_lang_t)idx); }

/* NOTE: switching language re-renders the current/forecast/detail data (temp,
 * condition text, humidity bucket, etc.) via notify_settings_changed(), but the
 * settings panel's OWN labels (field captions, "Deutsch/English/...") are only
 * built once in build_settings_panel(). If you need those to re-localize live too,
 * lv_obj_del(ui.settings_backdrop) and call build_settings_panel(ui.root) again
 * after weather_ui_set_language(). Left as a TODO to keep this port readable. */

void weather_ui_set_callbacks(weather_ui_search_cb_t on_search, weather_ui_select_city_cb_t on_select_city,
                               weather_ui_refresh_cb_t on_refresh, weather_ui_settings_changed_cb_t on_settings_changed) {
    ui.on_search = on_search;
    ui.on_select_city = on_select_city;
    ui.on_refresh = on_refresh;
    ui.on_settings_changed = on_settings_changed;
}

void weather_ui_set_language(weather_lang_t lang) { ui.lang = lang; notify_settings_changed(); }
void weather_ui_set_units(wx_temp_unit_t temp, wx_wind_unit_t wind, wx_time_fmt_t time_fmt) {
    ui.temp_unit = temp; ui.wind_unit = wind; ui.time_fmt = time_fmt; notify_settings_changed();
}
void weather_ui_set_current(const weather_current_t *cur) { ui.cur = *cur; ui.has_data = true; render_current(); }
void weather_ui_set_days(const weather_day_t days[WEATHER_UI_DAYS]) {
    memcpy(ui.days, days, sizeof(ui.days)); render_days();
}
void weather_ui_set_hourly(const weather_hourly_t *today, const weather_hourly_t *const days[WEATHER_UI_DAYS]) {
    if (today) { ui.hourly_today = *today; ui.hourly_today_valid = true; }
    else ui.hourly_today_valid = false;
    if (ui.has_data) hourly_chart_apply(ui.current_chart, ui.current_temp_series, ui.current_precip_chart, ui.current_precip_series, ui.hourly_today_valid ? &ui.hourly_today : NULL);
    if (days) {
        for (int i = 0; i < WEATHER_UI_DAYS; i++) {
            if (days[i]) { ui.hourly_days[i] = *days[i]; ui.hourly_days_valid[i] = true; }
            else ui.hourly_days_valid[i] = false;
        }
    }
}
void weather_ui_set_loading(bool loading) {
    if (ui.loading_lbl) lv_obj_set_style_opa(ui.current_card, loading && !ui.has_data ? LV_OPA_40 : LV_OPA_COVER, 0);
}
void weather_ui_set_error(const char *msg_or_null) {
    if (!ui.error_bar) return;
    if (msg_or_null) { lv_label_set_text(ui.error_lbl, msg_or_null); lv_obj_clear_flag(ui.error_bar, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(ui.error_bar, LV_OBJ_FLAG_HIDDEN);
}

void weather_ui_set_network_status(wx_net_status_t status) {
    if (!ui.header_net_dot) return;
    bool online = status == WX_NET_ONLINE;
    lv_obj_set_style_bg_color(ui.header_net_dot, online ? C_ACCENT : C_TEXT_MUTED, 0);
    lv_label_set_text(ui.header_net_lbl, online ? weather_strings[ui.lang].online : weather_strings[ui.lang].offline);
}

void weather_ui_set_data_stale(bool stale) {
    if (!ui.header_stale_tag) return;
    if (stale) lv_obj_clear_flag(ui.header_stale_tag, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ui.header_stale_tag, LV_OBJ_FLAG_HIDDEN);
}

static void build_settings_panel(lv_obj_t *parent) {
    ui.settings_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.settings_backdrop);
    lv_obj_set_size(ui.settings_backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ui.settings_backdrop, C_NEUTRAL_900, 0);
    lv_obj_set_style_bg_opa(ui.settings_backdrop, LV_OPA_50, 0);
    lv_obj_add_flag(ui.settings_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.settings_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.settings_backdrop, settings_close_cb, LV_EVENT_CLICKED, NULL);

    ui.settings_panel = lv_obj_create(ui.settings_backdrop);
    lv_obj_remove_style_all(ui.settings_panel);
    lv_obj_set_size(ui.settings_panel, 380, LV_SIZE_CONTENT);
    lv_obj_center(ui.settings_panel);
    lv_obj_set_style_bg_color(ui.settings_panel, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui.settings_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.settings_panel, R_LG, 0);
    lv_obj_set_style_pad_all(ui.settings_panel, 20, 0);
    lv_obj_set_flex_flow(ui.settings_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.settings_panel, 14, 0);
    lv_obj_add_flag(ui.settings_panel, LV_OBJ_FLAG_CLICKABLE); /* swallow clicks so they don't bubble to the backdrop */

    lv_obj_t *title_row = lv_obj_create(ui.settings_panel);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(title_row);
    lv_label_set_text(title, weather_strings[ui.lang].settings);
    lv_obj_set_style_text_color(title, C_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    icon_btn_create(title_row, LV_SYMBOL_CLOSE, settings_close_cb);

    static const char *lang_labels[4] = { "Deutsch", "English", "Espanol", "Francais" };
    lv_obj_t *f1 = field_wrap(ui.settings_panel, weather_strings[ui.lang].language);
    seg_create(f1, lang_labels, 4, ui.lang, on_seg_lang, NULL, ui.seg_lang);

    static const char *temp_labels[2] = { "\xC2\xB0" "C", "\xC2\xB0" "F" };
    lv_obj_t *f2 = field_wrap(ui.settings_panel, weather_strings[ui.lang].temperature);
    seg_create(f2, temp_labels, 2, ui.temp_unit, NULL, NULL, ui.seg_temp);

    static const char *wind_labels[3] = { "km/h", "mph", "m/s" };
    lv_obj_t *f3 = field_wrap(ui.settings_panel, weather_strings[ui.lang].wind_speed);
    seg_create(f3, wind_labels, 3, ui.wind_unit, NULL, NULL, ui.seg_wind);

    lv_obj_t *f4 = field_wrap(ui.settings_panel, weather_strings[ui.lang].time_format);
    const char *time_labels[2] = { weather_strings[ui.lang].h24, weather_strings[ui.lang].h12 };
    seg_create(f4, time_labels, 2, ui.time_fmt, NULL, NULL, ui.seg_time);

    lv_obj_t *f5 = field_wrap(ui.settings_panel, weather_strings[ui.lang].network);
    lv_obj_t *net_btn = lv_obj_create(f5);
    lv_obj_remove_style_all(net_btn);
    lv_obj_set_size(net_btn, LV_SIZE_CONTENT, 34);
    lv_obj_set_style_pad_hor(net_btn, 14, 0);
    lv_obj_set_style_border_width(net_btn, 1, 0);
    lv_obj_set_style_border_color(net_btn, C_DIVIDER, 0);
    lv_obj_set_style_radius(net_btn, R_MD, 0);
    lv_obj_add_flag(net_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(net_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(net_btn, open_wifi_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *net_btn_lbl = lv_label_create(net_btn);
    lv_label_set_text(net_btn_lbl, weather_strings[ui.lang].configure_network);
    lv_obj_set_style_text_color(net_btn_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(net_btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(net_btn_lbl);
}

/* ---- Wi-Fi setup dialog ---------------------------------------------------
 * Three sub-views stacked in one panel, matching the web version's flow:
 *   list   -> tap a scanned network (open: connect immediately; secured: go to password)
 *          -> "enter manually" goes to the manual-SSID view instead of a scan pick
 *   manual -> type an SSID, Next -> password view
 *   pass   -> type a password (with show/hide), Connect fires on_wifi_connect
 * All three share one on-screen keyboard instance where needed. */

static void wifi_show_view(lv_obj_t *view) {
    lv_obj_add_flag(ui.wifi_list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.wifi_manual_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.wifi_pass_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(view, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_close_cb(lv_event_t *e) { LV_UNUSED(e); lv_obj_add_flag(ui.wifi_backdrop, LV_OBJ_FLAG_HIDDEN); }

static void wifi_open_password_view(const char *ssid, bool secured) {
    LV_UNUSED(secured);
    lv_strlcpy(ui.wifi_pending_ssid, ssid, sizeof(ui.wifi_pending_ssid));
    ui.wifi_pass_plain[0] = '\0';
    ui.wifi_pass_visible = false;
    lv_textarea_set_text(ui.wifi_pass_ta, "");
    lv_textarea_set_password_mode(ui.wifi_pass_ta, true);
    lv_label_set_text(ui.wifi_pass_show_btn, weather_strings[ui.lang].show);
    lv_label_set_text_fmt(ui.wifi_pass_title_lbl, "%s \"%s\"", weather_strings[ui.lang].enter_password, ssid);
    lv_label_set_text(ui.wifi_connect_btn_lbl, weather_strings[ui.lang].connect);
    lv_obj_clear_flag(ui.wifi_connect_btn, LV_OBJ_FLAG_HIDDEN);
    wifi_show_view(ui.wifi_pass_view);
    lv_keyboard_set_textarea(ui.wifi_pass_kb, ui.wifi_pass_ta);
}

static void wifi_network_row_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= ui.wifi_networks_count) return;
    wx_wifi_network_t *net = &ui.wifi_networks[idx];
    if (net->secured) {
        wifi_open_password_view(net->ssid, true);
    } else if (ui.on_wifi_connect) {
        ui.on_wifi_connect(net->ssid, "");
    }
}

static void wifi_manual_open_cb(lv_event_t *e) {
    LV_UNUSED(e);
    lv_textarea_set_text(ui.wifi_manual_ta, "");
    wifi_show_view(ui.wifi_manual_view);
    lv_keyboard_set_textarea(ui.wifi_manual_kb, ui.wifi_manual_ta);
}

static void wifi_manual_back_cb(lv_event_t *e) { LV_UNUSED(e); wifi_show_view(ui.wifi_list_view); }

static void wifi_manual_next_cb(lv_event_t *e) {
    LV_UNUSED(e);
    const char *ssid = lv_textarea_get_text(ui.wifi_manual_ta);
    if (ssid[0] == '\0') return;
    wifi_open_password_view(ssid, true);
}

static void wifi_pass_back_cb(lv_event_t *e) { LV_UNUSED(e); wifi_show_view(ui.wifi_list_view); }

static void wifi_pass_show_toggle_cb(lv_event_t *e) {
    LV_UNUSED(e);
    ui.wifi_pass_visible = !ui.wifi_pass_visible;
    lv_textarea_set_password_mode(ui.wifi_pass_ta, !ui.wifi_pass_visible);
    lv_label_set_text(ui.wifi_pass_show_btn, ui.wifi_pass_visible ? weather_strings[ui.lang].hide : weather_strings[ui.lang].show);
}

static void wifi_connect_click_cb(lv_event_t *e) {
    LV_UNUSED(e);
    const char *pass = lv_textarea_get_text(ui.wifi_pass_ta);
    lv_strlcpy(ui.wifi_pass_plain, pass, sizeof(ui.wifi_pass_plain));
    lv_label_set_text(ui.wifi_connect_btn_lbl, weather_strings[ui.lang].connecting);
    lv_obj_add_state(ui.wifi_connect_btn, LV_STATE_DISABLED);
    if (ui.on_wifi_connect) ui.on_wifi_connect(ui.wifi_pending_ssid, pass);
}

static lv_obj_t *wifi_row_create(lv_obj_t *parent, const char *ssid, int strength, bool secured, int idx) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, C_NEUTRAL_900, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, R_MD, 0);
    lv_obj_set_style_pad_all(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, wifi_network_row_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ssid_lbl = lv_label_create(left);
    lv_label_set_text(ssid_lbl, ssid);
    lv_obj_set_style_text_color(ssid_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_14, 0);
    lv_obj_t *sec_lbl = lv_label_create(left);
    lv_label_set_text(sec_lbl, secured ? weather_strings[ui.lang].secured : weather_strings[ui.lang].open_net);
    lv_obj_set_style_text_color(sec_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(sec_lbl, &lv_font_montserrat_10, 0);

    lv_obj_t *bars = lv_label_create(row);
    lv_label_set_text(bars, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(bars, strength >= 2 ? C_ACCENT : C_TEXT_MUTED, 0);
    return row;
}

void weather_ui_set_wifi_scan_results(const wx_wifi_network_t *networks, int count) {
    if (!ui.wifi_list_box) return;
    lv_obj_clean(ui.wifi_list_box);
    ui.wifi_networks_count = count > 32 ? 32 : count;
    for (int i = 0; i < ui.wifi_networks_count; i++) ui.wifi_networks[i] = networks[i];
    lv_label_set_text(ui.wifi_scan_status_lbl, count == 0 ? weather_strings[ui.lang].no_networks : "");
    for (int i = 0; i < ui.wifi_networks_count; i++) {
        wx_wifi_network_t *n = &ui.wifi_networks[i];
        wifi_row_create(ui.wifi_list_box, n->ssid, n->strength, n->secured, i);
    }
}

void weather_ui_set_wifi_connect_result(bool success) {
    lv_obj_clear_state(ui.wifi_connect_btn, LV_STATE_DISABLED);
    if (success) {
        lv_label_set_text(ui.wifi_connect_btn_lbl, weather_strings[ui.lang].connected);
        lv_obj_add_flag(ui.wifi_backdrop, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(ui.wifi_connect_btn_lbl, weather_strings[ui.lang].connect);
    }
}

void weather_ui_set_wifi_callbacks(weather_ui_wifi_scan_cb_t on_scan, weather_ui_wifi_connect_cb_t on_connect) {
    ui.on_wifi_scan = on_scan;
    ui.on_wifi_connect = on_connect;
}

static lv_obj_t *wifi_text_btn_create(lv_obj_t *parent, const char *text, lv_event_cb_t cb, bool ghost) {
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_pad_hor(btn, 16, 0);
    lv_obj_set_style_radius(btn, R_MD, 0);
    if (ghost) {
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, C_DIVIDER, 0);
    } else {
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, C_ACCENT, 0);
        lv_obj_set_style_bg_color(btn, C_ACCENT_900, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    }
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    return btn;
}

static void build_wifi_dialog(lv_obj_t *parent) {
    ui.wifi_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.wifi_backdrop);
    lv_obj_set_size(ui.wifi_backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ui.wifi_backdrop, C_NEUTRAL_900, 0);
    lv_obj_set_style_bg_opa(ui.wifi_backdrop, LV_OPA_50, 0);
    lv_obj_add_flag(ui.wifi_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.wifi_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.wifi_backdrop, wifi_close_cb, LV_EVENT_CLICKED, NULL);

    ui.wifi_panel = lv_obj_create(ui.wifi_backdrop);
    lv_obj_remove_style_all(ui.wifi_panel);
    lv_obj_set_size(ui.wifi_panel, 460, 480);
    lv_obj_center(ui.wifi_panel);
    lv_obj_set_style_bg_color(ui.wifi_panel, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui.wifi_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.wifi_panel, R_LG, 0);
    lv_obj_set_style_pad_all(ui.wifi_panel, 20, 0);
    lv_obj_set_flex_flow(ui.wifi_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.wifi_panel, 12, 0);
    lv_obj_add_flag(ui.wifi_panel, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title_row = lv_obj_create(ui.wifi_panel);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(title_row);
    lv_label_set_text(title, weather_strings[ui.lang].wifi_title);
    lv_obj_set_style_text_color(title, C_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    wifi_text_btn_create(title_row, weather_strings[ui.lang].cancel2, wifi_close_cb, true);

    /* -- list view: scan button + status + scrollable results + manual-entry link -- */
    ui.wifi_list_view = lv_obj_create(ui.wifi_panel);
    lv_obj_remove_style_all(ui.wifi_list_view);
    lv_obj_set_size(ui.wifi_list_view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(ui.wifi_list_view, 1);
    lv_obj_set_flex_flow(ui.wifi_list_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.wifi_list_view, 10, 0);
    lv_obj_clear_flag(ui.wifi_list_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *scan_row = lv_obj_create(ui.wifi_list_view);
    lv_obj_remove_style_all(scan_row);
    lv_obj_set_size(scan_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scan_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scan_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(scan_row, LV_OBJ_FLAG_SCROLLABLE);
    ui.wifi_scan_status_lbl = lv_label_create(scan_row);
    lv_obj_set_style_text_color(ui.wifi_scan_status_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ui.wifi_scan_status_lbl, &lv_font_montserrat_12, 0);
    lv_label_set_text(ui.wifi_scan_status_lbl, "");
    ui.wifi_scan_btn = wifi_text_btn_create(scan_row, weather_strings[ui.lang].scan, open_wifi_cb, true);

    ui.wifi_list_box = lv_obj_create(ui.wifi_list_view);
    lv_obj_remove_style_all(ui.wifi_list_box);
    lv_obj_set_size(ui.wifi_list_box, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(ui.wifi_list_box, 1);
    lv_obj_set_flex_flow(ui.wifi_list_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.wifi_list_box, 8, 0);
    lv_obj_set_scroll_dir(ui.wifi_list_box, LV_DIR_VER);

    wifi_text_btn_create(ui.wifi_list_view, weather_strings[ui.lang].enter_manually, wifi_manual_open_cb, true);

    /* -- manual SSID entry view -- */
    ui.wifi_manual_view = lv_obj_create(ui.wifi_panel);
    lv_obj_remove_style_all(ui.wifi_manual_view);
    lv_obj_set_size(ui.wifi_manual_view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(ui.wifi_manual_view, 1);
    lv_obj_set_flex_flow(ui.wifi_manual_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.wifi_manual_view, 12, 0);
    lv_obj_clear_flag(ui.wifi_manual_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui.wifi_manual_view, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *manual_field = field_wrap(ui.wifi_manual_view, weather_strings[ui.lang].enter_ssid);
    ui.wifi_manual_ta = lv_textarea_create(manual_field);
    lv_textarea_set_one_line(ui.wifi_manual_ta, true);
    lv_obj_set_width(ui.wifi_manual_ta, LV_PCT(100));

    lv_obj_t *manual_btn_row = lv_obj_create(ui.wifi_manual_view);
    lv_obj_remove_style_all(manual_btn_row);
    lv_obj_set_size(manual_btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(manual_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(manual_btn_row, 10, 0);
    lv_obj_clear_flag(manual_btn_row, LV_OBJ_FLAG_SCROLLABLE);
    wifi_text_btn_create(manual_btn_row, weather_strings[ui.lang].back, wifi_manual_back_cb, true);
    wifi_text_btn_create(manual_btn_row, weather_strings[ui.lang].next, wifi_manual_next_cb, false);

    ui.wifi_manual_kb = lv_keyboard_create(ui.wifi_manual_view);
    lv_obj_set_size(ui.wifi_manual_kb, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(ui.wifi_manual_kb, 1);
    lv_keyboard_set_mode(ui.wifi_manual_kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    /* -- password entry view -- */
    ui.wifi_pass_view = lv_obj_create(ui.wifi_panel);
    lv_obj_remove_style_all(ui.wifi_pass_view);
    lv_obj_set_size(ui.wifi_pass_view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(ui.wifi_pass_view, 1);
    lv_obj_set_flex_flow(ui.wifi_pass_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.wifi_pass_view, 12, 0);
    lv_obj_clear_flag(ui.wifi_pass_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui.wifi_pass_view, LV_OBJ_FLAG_HIDDEN);

    ui.wifi_pass_title_lbl = lv_label_create(ui.wifi_pass_view);
    lv_label_set_long_mode(ui.wifi_pass_title_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui.wifi_pass_title_lbl, LV_PCT(100));
    lv_obj_set_style_text_color(ui.wifi_pass_title_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.wifi_pass_title_lbl, &lv_font_montserrat_14, 0);

    lv_obj_t *pass_row = lv_obj_create(ui.wifi_pass_view);
    lv_obj_remove_style_all(pass_row);
    lv_obj_set_size(pass_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pass_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pass_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pass_row, 8, 0);
    lv_obj_clear_flag(pass_row, LV_OBJ_FLAG_SCROLLABLE);
    ui.wifi_pass_ta = lv_textarea_create(pass_row);
    lv_textarea_set_one_line(ui.wifi_pass_ta, true);
    lv_textarea_set_password_mode(ui.wifi_pass_ta, true);
    lv_obj_set_flex_grow(ui.wifi_pass_ta, 1);
    lv_obj_t *show_btn = lv_obj_create(pass_row);
    lv_obj_remove_style_all(show_btn);
    lv_obj_set_size(show_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(show_btn, 8, 0);
    lv_obj_set_style_border_width(show_btn, 1, 0);
    lv_obj_set_style_border_color(show_btn, C_DIVIDER, 0);
    lv_obj_set_style_radius(show_btn, R_MD, 0);
    lv_obj_add_flag(show_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(show_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(show_btn, wifi_pass_show_toggle_cb, LV_EVENT_CLICKED, NULL);
    ui.wifi_pass_show_btn = lv_label_create(show_btn);
    lv_label_set_text(ui.wifi_pass_show_btn, weather_strings[ui.lang].show);
    lv_obj_set_style_text_color(ui.wifi_pass_show_btn, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.wifi_pass_show_btn, &lv_font_montserrat_12, 0);

    lv_obj_t *pass_btn_row = lv_obj_create(ui.wifi_pass_view);
    lv_obj_remove_style_all(pass_btn_row);
    lv_obj_set_size(pass_btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pass_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pass_btn_row, 10, 0);
    lv_obj_clear_flag(pass_btn_row, LV_OBJ_FLAG_SCROLLABLE);
    wifi_text_btn_create(pass_btn_row, weather_strings[ui.lang].back, wifi_pass_back_cb, true);
    ui.wifi_connect_btn = wifi_text_btn_create(pass_btn_row, weather_strings[ui.lang].connect, wifi_connect_click_cb, false);
    ui.wifi_connect_btn_lbl = lv_obj_get_child(ui.wifi_connect_btn, 0);

    ui.wifi_pass_kb = lv_keyboard_create(ui.wifi_pass_view);
    lv_obj_set_size(ui.wifi_pass_kb, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(ui.wifi_pass_kb, 1);
    lv_keyboard_set_mode(ui.wifi_pass_kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    wifi_show_view(ui.wifi_list_view);
}

/* ---- error bar ------------------------------------------------------------ */

static void build_error_bar(lv_obj_t *parent) {
    ui.error_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.error_bar);
    lv_obj_set_size(ui.error_bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(ui.error_bar, C_ACCENT_900, 0);
    lv_obj_set_style_bg_opa(ui.error_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui.error_bar, 1, 0);
    lv_obj_set_style_border_color(ui.error_bar, C_ACCENT_700, 0);
    lv_obj_set_style_radius(ui.error_bar, R_MD, 0);
    lv_obj_set_style_pad_all(ui.error_bar, 12, 0);
    lv_obj_set_flex_flow(ui.error_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.error_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(ui.error_bar, LV_OBJ_FLAG_HIDDEN);
    ui.error_lbl = lv_label_create(ui.error_bar);
    lv_obj_set_style_text_color(ui.error_lbl, C_TEXT, 0);
    lv_obj_t *retry = lv_obj_create(ui.error_bar);
    lv_obj_remove_style_all(retry);
    lv_obj_set_style_pad_all(retry, 6, 0);
    lv_obj_set_style_border_width(retry, 1, 0);
    lv_obj_set_style_border_color(retry, C_DIVIDER, 0);
    lv_obj_set_style_radius(retry, R_MD, 0);
    lv_obj_add_flag(retry, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(retry, refresh_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *retry_lbl = lv_label_create(retry);
    lv_label_set_text(retry_lbl, weather_strings[ui.lang].retry);
    lv_obj_set_style_text_color(retry_lbl, C_TEXT, 0);
}

/* ---- entry point ---------------------------------------------------------- */

void weather_ui_create(lv_obj_t *parent) {
    memset(&ui, 0, sizeof(ui));
    ui.lang = LANG_EN;
    ui.temp_unit = WX_UNIT_C;
    ui.wind_unit = WX_WIND_KMH;
    ui.time_fmt = WX_TIME_24;

    ui.root = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.root);
    lv_obj_set_size(ui.root, 1024, 600);
    lv_obj_set_style_bg_color(ui.root, C_BG, 0);
    lv_obj_set_style_bg_opa(ui.root, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(ui.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(ui.root, LV_OBJ_FLAG_SCROLLABLE);

    build_header(ui.root);
    build_error_bar(ui.root);

    lv_obj_t *content = lv_obj_create(ui.root);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_pad_all(content, 24, 0);
    lv_obj_set_style_pad_top(content, 8, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 14, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    build_current_card(content);
    build_forecast_strip(content);

    build_settings_panel(ui.root);
    build_wifi_dialog(ui.root);
    build_search_overlay(ui.root);
    build_detail_panel(ui.root);
}
