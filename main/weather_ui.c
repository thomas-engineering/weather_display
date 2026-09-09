/* Weather UI — LVGL v9.x port of the "Weather App" design.
 *
 * Imported from the Claude Design project's lvgl_export/, then adapted for the
 * Waveshare ESP32-P4-WIFI6-Touch-LCD-7B firmware. Deviations from the export are
 * marked with "FIX:" comments; everything else is the design's own layout and
 * styling, unchanged.
 *
 * Scope: this file owns layout, styling and interaction only. Networking
 * (Open-Meteo forecast + geocoding) and date/weekday formatting live in
 * app_weather.c / app_format.c and arrive here through weather_ui_set_*().
 *
 * Colors are the Nocturne design system's token values, hardcoded (LVGL has no
 * CSS variables).
 */

#include "weather_ui.h"
#include "weather_icons.h"
#include "ui_fonts.h"
#include "weather_chart.h"
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
#define C_NEUTRAL_800 lv_color_hex(0x3F424D)
#define C_NEUTRAL_600 lv_color_hex(0x75798C)
#define C_NEUTRAL_300 lv_color_hex(0xCFD3E5)
#define C_GOOD        lv_color_hex(0x7FBF8F)  /* status: online */
#define C_WARN        lv_color_hex(0xD9C184)  /* status: stale data */
#define R_MD 8
#define R_LG 14

#define DETAIL_H 468 /* design: .dialog sheet is max-height:78% of the 600px canvas */

typedef struct {
    lv_obj_t *root, *header_location_lbl, *header_time_lbl, *header_date_lbl;
    lv_obj_t *current_card, *current_icon, *current_cond_tag, *current_temp_lbl, *current_feel_lbl;
    lv_obj_t *stat_humidity_val, *stat_humidity_sub, *stat_feel_val, *stat_wind_val, *stat_precip_val;
    lv_obj_t *stat_humidity_cap, *stat_feel_cap, *stat_wind_cap, *stat_precip_cap;
    lv_obj_t *forecast_title, *forecast_strip;
    lv_obj_t *day_cards[WEATHER_UI_DAYS];
    lv_obj_t *day_icons[WEATHER_UI_DAYS];
    /* FIX: the export recovered these by child index ("[0]=day,[1]=date,..."), which
     * breaks the moment the card's child order changes. Keep explicit pointers. */
    lv_obj_t *day_name_lbl[WEATHER_UI_DAYS], *day_date_lbl[WEATHER_UI_DAYS];
    lv_obj_t *day_hi_lbl[WEATHER_UI_DAYS], *day_lo_lbl[WEATHER_UI_DAYS], *day_precip_lbl[WEATHER_UI_DAYS];

    lv_obj_t *error_bar, *error_lbl, *error_retry_lbl;
    lv_obj_t *refresh_toast, *refresh_toast_lbl;
    lv_timer_t *refresh_toast_timer;

    /* Header location is split so the LV_SYMBOL_* pin can keep a Montserrat font:
     * the generated Inter faces carry Latin-1 but not the FontAwesome codepoints. */
    lv_obj_t *header_loc_icon, *header_net_dot, *header_net_lbl, *header_stale_lbl;

    lv_obj_t *today_chart, *detail_chart;
    weather_hourly_t hourly_today;
    weather_hourly_t hourly_days[WEATHER_UI_DAYS];
    bool hourly_today_valid, hourly_day_valid[WEATHER_UI_DAYS];

    lv_obj_t *wifi_backdrop, *wifi_title_lbl, *wifi_status_lbl, *wifi_list;
    lv_obj_t *wifi_list_view, *wifi_pw_view;
    lv_obj_t *wifi_scan_btn_lbl, *wifi_manual_btn_lbl, *wifi_back_lbl, *wifi_back2_lbl, *wifi_forget_btn_lbl;
    lv_obj_t *wifi_pw_prompt, *wifi_pw_ta, *wifi_pw_kb, *wifi_pw_show_lbl, *wifi_connect_lbl;
    char wifi_pending_ssid[33];
    bool wifi_manual_mode;

    lv_obj_t *settings_backdrop, *settings_panel;
    lv_obj_t *seg_lang[4], *seg_temp[2], *seg_wind[3], *seg_time[2];
    lv_obj_t *brightness_slider, *brightness_adaptive_sw;

    lv_obj_t *device_info_backdrop;
    lv_obj_t *device_info_online_group, *device_info_offline_lbl;
    lv_obj_t *device_info_name_val, *device_info_hw_val, *device_info_fw_val;
    lv_obj_t *device_info_ip_val, *device_info_dns_val, *device_info_gw_val, *device_info_note_lbl;
    bool has_device_info;
    char device_info_name[64], device_info_hw[32], device_info_fw[32];
    bool device_info_online;
    char device_info_ip[16], device_info_dns[16], device_info_gw[16];
    char device_info_note[160];

    lv_obj_t *search_backdrop, *search_ta, *search_kb, *search_results, *search_status_lbl, *search_cancel_lbl;

    lv_obj_t *detail_backdrop, *detail_panel, *detail_icon, *detail_day_lbl, *detail_hilo_lbl, *detail_cond_lbl;
    lv_obj_t *detail_feel_lbl, *detail_precip_lbl, *detail_wind_lbl;
    lv_obj_t *detail_feel_cap, *detail_precip_cap, *detail_wind_cap;

    weather_current_t cur;
    weather_day_t days[WEATHER_UI_DAYS];
    bool has_data;
    bool days_valid;
    weather_lang_t lang;
    wx_temp_unit_t temp_unit;
    wx_wind_unit_t wind_unit;
    wx_time_fmt_t time_fmt;
    int brightness; /* 10-100, mirrors the slider; also its value before it exists */
    bool brightness_adaptive; /* mirrors the switch's checked state; also its value before it exists */
    bool brightness_adaptive_available; /* false: no camera found, switch stays disabled */

    weather_ui_search_cb_t on_search;
    weather_ui_select_city_cb_t on_select_city;
    weather_ui_refresh_cb_t on_refresh;
    weather_ui_settings_changed_cb_t on_settings_changed;
    weather_ui_wifi_scan_cb_t on_wifi_scan;
    weather_ui_wifi_connect_cb_t on_wifi_connect;
    weather_ui_wifi_forget_cb_t on_wifi_forget;
    weather_ui_brightness_cb_t on_brightness;
    weather_ui_brightness_adaptive_cb_t on_brightness_adaptive;
} weather_ui_t;

static weather_ui_t ui;

static void build_settings_panel(lv_obj_t *parent);
static void build_device_info_panel(lv_obj_t *parent);
static void device_info_rebuild(bool keep_open);
static void open_device_info_cb(lv_event_t *e);
static void close_device_info_cb(lv_event_t *e);
static void build_wifi_screen(lv_obj_t *parent);
static void wifi_show_list(void);
static void wifi_relabel(void);
static void add_event_bubble_recursive(lv_obj_t *obj);

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

typedef struct { seg_cb_t cb; void *user; int count; lv_obj_t **group; } seg_ctx_t;

/* FIX: the export kept these in a fixed `static seg_ctx_t ctxs[16]` pool that was
 * never reset, so rebuilding the settings panel (which this port now does on every
 * language change) would run off the end of it. One heap ctx per button instead,
 * freed with the button. */
static void seg_ctx_free_cb(lv_event_t *e) { lv_free(lv_obj_get_user_data(lv_event_get_target_obj(e))); }

static void seg_click_cb(lv_event_t *e) {
    lv_obj_t *clicked = lv_event_get_target_obj(e);
    seg_ctx_t *ctx = (seg_ctx_t *)lv_obj_get_user_data(clicked);
    if (!ctx) return;
    int idx = -1;
    for (int i = 0; i < ctx->count; i++) {
        lv_obj_set_style_bg_opa(ctx->group[i], LV_OPA_TRANSP, 0);
        if (ctx->group[i] == clicked) idx = i;
    }
    lv_obj_set_style_bg_opa(clicked, LV_OPA_20, 0);
    if (ctx->cb && idx >= 0) ctx->cb(idx, ctx->user);
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
    /* Synced from Claude Design 2026-09-10: option padding 12/16, >=44px touch
     * target — was a fixed 34 with 12px horizontal padding. */
    lv_obj_set_size(row, LV_SIZE_CONTENT, 44);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < count; i++) {
        lv_obj_t *btn = lv_obj_create(row);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_PCT(100));
        lv_obj_set_style_pad_hor(btn, 16, 0);
        lv_obj_set_style_bg_color(btn, C_ACCENT, 0);
        lv_obj_set_style_bg_opa(btn, i == selected ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_side(btn, i == 0 ? LV_BORDER_SIDE_NONE : LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(btn, C_DIVIDER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_color(lbl, C_TEXT, 0);
        lv_obj_set_style_text_font(lbl, FONT_14, 0);
        lv_obj_center(lbl);

        seg_ctx_t *ctx = lv_malloc(sizeof(seg_ctx_t));
        if (ctx) {
            ctx->cb = cb; ctx->user = user; ctx->count = count; ctx->group = out;
            lv_obj_set_user_data(btn, ctx);
            lv_obj_add_event_cb(btn, seg_ctx_free_cb, LV_EVENT_DELETE, NULL);
        }
        out[i] = btn;
        lv_obj_add_event_cb(btn, seg_click_cb, LV_EVENT_CLICKED, NULL);
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
    lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = lv_label_create(wrap);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl, FONT_12, 0);
    return wrap;
}

/* One "caption ... value" row for the Device information dialog (design's
 * `justify-content:space-between` div pairs). Returns the value label so the
 * caller can fill/update its text. */
static lv_obj_t *info_row_create(lv_obj_t *parent, const char *caption) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cap = lv_label_create(row);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(cap, FONT_12, 0);
    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, "");   /* not "Text" (LVGL's own default) until real data arrives */
    lv_obj_set_style_text_color(val, C_TEXT, 0);
    lv_obj_set_style_text_font(val, FONT_14, 0);
    return val;
}

/* ---- header ------------------------------------------------------------ */

static void refresh_click_cb(lv_event_t *e) { LV_UNUSED(e); if (ui.on_refresh) ui.on_refresh(); }
static void open_search_cb(lv_event_t *e);
static void open_settings_cb(lv_event_t *e);

static lv_obj_t *icon_btn_create(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    /* Synced from Claude Design 2026-09-10: touch targets enlarged again, now
     * 44x44 -> 56x56 (icon glyph 20px -> 26px to match), was 36x36 before that. */
    lv_obj_set_size(btn, 56, 56);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, C_DIVIDER, 0);
    lv_obj_set_style_radius(btn, R_MD, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_color(lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(lbl, FONT_SYMBOL_LG, 0);   /* LV_SYMBOL_* live in Montserrat only */
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static void build_header(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    /* Was a fixed 64: with the 2026-09-10 touch-target sync the 44px icon buttons
     * and the location button's own new padding no longer fit inside that, so the
     * row now sizes itself to its tallest child instead of clipping it. */
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    /* FIX: the design pads the header --space-6 --space-6 --space-3 (16.8 / 8.4). */
    lv_obj_set_style_pad_hor(row, 17, 0);
    lv_obj_set_style_pad_top(row, 17, 0);
    lv_obj_set_style_pad_bottom(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *loc_btn = lv_obj_create(row);
    lv_obj_remove_style_all(loc_btn);
    lv_obj_set_size(loc_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_flag(loc_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(loc_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(loc_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(loc_btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(loc_btn, 8, 0);
    /* Synced from Claude Design 2026-09-10: padding 6/8 -> 12/14, >=44px touch target. */
    lv_obj_set_style_pad_ver(loc_btn, 12, 0);
    lv_obj_set_style_pad_hor(loc_btn, 14, 0);
    /* FIX: the export put the GPS pin and the city name in one label. The Inter faces
     * generated for Latin-1 have no FontAwesome codepoints, so the symbol needs its own
     * label on a Montserrat font while the name gets the design's typeface. */
    ui.header_loc_icon = lv_label_create(loc_btn);
    lv_label_set_text(ui.header_loc_icon, LV_SYMBOL_GPS);
    lv_obj_set_style_text_color(ui.header_loc_icon, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.header_loc_icon, FONT_SYMBOL, 0);
    ui.header_location_lbl = lv_label_create(loc_btn);
    lv_label_set_text(ui.header_location_lbl, "--");
    lv_obj_set_style_text_color(ui.header_location_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.header_location_lbl, FONT_18, 0);
    lv_obj_add_event_cb(loc_btn, open_search_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *right = lv_obj_create(row);
    lv_obj_remove_style_all(right);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 11, 0);   /* FIX: --space-4, was 16 */
    lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    /* FIX: the design puts the online/offline + stale-data indicators in the
     * right-hand group (next to the clock, before the refresh/settings icons),
     * not next to the location on the left — this had it pinned to the left
     * via flex_grow, opposite of Weather App.dc.html's header row. */
    lv_obj_t *status_wrap = lv_obj_create(right);
    lv_obj_remove_style_all(status_wrap);
    lv_obj_set_size(status_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status_wrap, 6, 0);
    lv_obj_remove_flag(status_wrap, LV_OBJ_FLAG_SCROLLABLE);

    /* Status is never colour-alone: the dot is paired with its own text label. */
    ui.header_net_dot = lv_obj_create(status_wrap);
    lv_obj_remove_style_all(ui.header_net_dot);
    lv_obj_set_size(ui.header_net_dot, 8, 8);
    lv_obj_set_style_radius(ui.header_net_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui.header_net_dot, C_GOOD, 0);
    lv_obj_set_style_bg_opa(ui.header_net_dot, LV_OPA_COVER, 0);

    ui.header_net_lbl = lv_label_create(status_wrap);
    lv_label_set_text(ui.header_net_lbl, weather_strings[ui.lang].online);
    lv_obj_set_style_text_color(ui.header_net_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ui.header_net_lbl, FONT_12, 0);

    ui.header_stale_lbl = lv_label_create(status_wrap);
    lv_label_set_text(ui.header_stale_lbl, weather_strings[ui.lang].data_outdated);
    lv_obj_set_style_text_color(ui.header_stale_lbl, C_WARN, 0);
    lv_obj_set_style_text_font(ui.header_stale_lbl, FONT_12, 0);
    lv_obj_add_flag(ui.header_stale_lbl, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *time_wrap = lv_obj_create(right);
    lv_obj_remove_style_all(time_wrap);
    lv_obj_set_size(time_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(time_wrap, 2, 0);
    /* Synced from Claude Design 2026-09-10: extra --space-6 (17px) on top of
     * the row's own --space-4 gap, on both sides of the clock/date group. */
    lv_obj_set_style_margin_left(time_wrap, 17, 0);
    lv_obj_remove_flag(time_wrap, LV_OBJ_FLAG_SCROLLABLE);
    ui.header_time_lbl = lv_label_create(time_wrap);
    lv_label_set_text(ui.header_time_lbl, "--:--");
    lv_obj_set_style_text_color(ui.header_time_lbl, C_TEXT, 0);
    /* Synced from Claude Design 2026-09-10: clock 20->30, date 12->18. */
    lv_obj_set_style_text_font(ui.header_time_lbl, FONT_30, 0);
    ui.header_date_lbl = lv_label_create(time_wrap);
    lv_label_set_text(ui.header_date_lbl, "");
    lv_obj_set_style_text_color(ui.header_date_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ui.header_date_lbl, FONT_18, 0);

    lv_obj_t *refresh_btn = icon_btn_create(right, LV_SYMBOL_REFRESH, refresh_click_cb);
    lv_obj_set_style_margin_left(refresh_btn, 17, 0); /* --space-6, same reasoning as time_wrap above */
    icon_btn_create(right, LV_SYMBOL_SETTINGS, open_settings_cb);
}

/* ---- current weather card ---------------------------------------------- */

/* `sub_out` is optional and only the humidity box uses it: the design puts the
 * category word and the raw percentage in the same box, one under the other.
 *
 * `big` selects between the two sizings the design now uses for this same card
 * in different places: the main screen's current-weather row (synced from
 * Claude Design 2026-09-10 — wider columns, larger type) and the still-compact
 * day-detail sheet, which this update didn't touch. */
static lv_obj_t *stat_box_create(lv_obj_t *parent, const char *label, lv_obj_t **val_out,
                                  lv_obj_t **cap_out, lv_obj_t **sub_out,
                                  int32_t width, int32_t pad_hor, bool big) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, width, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(box, C_NEUTRAL_900, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, R_MD, 0);
    lv_obj_set_style_pad_ver(box, big ? 10 : 8, 0);
    lv_obj_set_style_pad_hor(box, pad_hor, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, label);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_color(lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl, big ? FONT_12 : FONT_10, 0);
    lv_obj_t *val = lv_label_create(box);
    lv_label_set_text(val, "--");
    lv_label_set_long_mode(val, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(val, LV_PCT(100));
    lv_obj_set_style_text_color(val, C_TEXT, 0);
    lv_obj_set_style_text_font(val, big ? FONT_16 : FONT_14, 0);
    *val_out = val;
    if (cap_out) *cap_out = lbl;
    if (sub_out) {
        /* FIX: the export dropped this line; the design has it. */
        lv_obj_t *sub = lv_label_create(box);
        lv_label_set_text(sub, "");
        lv_obj_set_style_text_color(sub, C_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(sub, big ? FONT_14 : FONT_10, 0);
        *sub_out = sub;
    }
    return box;
}

static void day_card_click_cb(lv_event_t *e);

static void build_current_card(lv_obj_t *parent) {
    ui.current_card = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.current_card);
    lv_obj_set_size(ui.current_card, LV_PCT(100), 150);
    lv_obj_set_style_bg_color(ui.current_card, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui.current_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.current_card, R_MD, 0);
    lv_obj_set_style_pad_hor(ui.current_card, 24, 0);
    lv_obj_set_flex_flow(ui.current_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.current_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    /* Synced from Claude Design 2026-09-10: gap 20 -> 16. */
    lv_obj_set_style_pad_column(ui.current_card, 16, 0);
    lv_obj_remove_flag(ui.current_card, LV_OBJ_FLAG_SCROLLABLE);

    /* FIX: the design has no color override on the icon — it inherits the
     * ambient text color, same as the temperature next to it. C_ACCENT made
     * the icon purple while the temperature stayed C_TEXT. */
    ui.current_icon = weather_icon_create(ui.current_card, WX_ICON_CLOUD, 64, C_TEXT);

    lv_obj_t *temp_wrap = lv_obj_create(ui.current_card);
    lv_obj_remove_style_all(temp_wrap);
    /* Synced from Claude Design 2026-09-10: width 170 -> 150. */
    lv_obj_set_size(temp_wrap, 150, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(temp_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(temp_wrap, LV_OBJ_FLAG_SCROLLABLE);
    ui.current_cond_tag = lv_label_create(temp_wrap);
    lv_label_set_text(ui.current_cond_tag, "");
    lv_obj_set_style_text_color(ui.current_cond_tag, C_ACCENT_100, 0);
    lv_obj_set_style_bg_color(ui.current_cond_tag, C_ACCENT_800, 0);
    lv_obj_set_style_bg_opa(ui.current_cond_tag, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.current_cond_tag, 6, 0);
    lv_obj_set_style_pad_hor(ui.current_cond_tag, 10, 0);
    lv_obj_set_style_pad_ver(ui.current_cond_tag, 3, 0);
    lv_obj_set_style_text_font(ui.current_cond_tag, FONT_12, 0);
    ui.current_temp_lbl = lv_label_create(temp_wrap);
    lv_label_set_text(ui.current_temp_lbl, "--\xC2\xB0");
    lv_obj_set_style_text_color(ui.current_temp_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.current_temp_lbl, FONT_48, 0);
    lv_obj_set_style_pad_top(ui.current_temp_lbl, 6, 0);

    ui.current_feel_lbl = lv_label_create(ui.current_card);
    lv_label_set_text(ui.current_feel_lbl, weather_strings[ui.lang].loading);
    lv_label_set_long_mode(ui.current_feel_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(ui.current_feel_lbl, 1);
    lv_obj_set_style_text_color(ui.current_feel_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ui.current_feel_lbl, FONT_14, 0);
    lv_obj_set_style_pad_right(ui.current_feel_lbl, 16, 0);
    /* Synced from Claude Design 2026-09-10: line-height 1.0 -> 1.45. */
    lv_obj_set_style_text_line_space(ui.current_feel_lbl, 6, 0);

    lv_obj_t *stats = lv_obj_create(ui.current_card);
    lv_obj_remove_style_all(stats);
    lv_obj_set_size(stats, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(stats, 8, 0);
    lv_obj_remove_flag(stats, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *stat_boxes[4];
    /* Synced from Claude Design 2026-09-10: grid-template-columns went from an
     * equal repeat(4,104px) to 128px 96px 96px 96px — Humidity carries a third
     * line (the raw percentage) the others don't, so it gets the extra width
     * instead of the whole row growing taller than its siblings. */
    stat_boxes[0] = stat_box_create(stats, weather_strings[ui.lang].humidity, &ui.stat_humidity_val, &ui.stat_humidity_cap, &ui.stat_humidity_sub, 128, 12, true);
    stat_boxes[1] = stat_box_create(stats, weather_strings[ui.lang].feels_like, &ui.stat_feel_val, &ui.stat_feel_cap, NULL, 96, 8, true);
    stat_boxes[2] = stat_box_create(stats, weather_strings[ui.lang].wind, &ui.stat_wind_val, &ui.stat_wind_cap, NULL, 96, 8, true);
    stat_boxes[3] = stat_box_create(stats, weather_strings[ui.lang].precip, &ui.stat_precip_val, &ui.stat_precip_cap, NULL, 96, 8, true);
    /* FIX: the design is a CSS grid, which stretches every cell in a row to the
     * tallest one by default — the Humidity box is 3 lines (label/value/pct)
     * against 2 for the other three, so on the LCD it ends up visibly taller than
     * its siblings. This LVGL version's flex has no stretch cross-align (only
     * START/END/CENTER/SPACE_*), so match the grid's effect by measuring the
     * tallest box after layout and pinning all four to that height. */
    lv_obj_update_layout(stats);
    int32_t max_h = 0;
    for (int i = 0; i < 4; i++) max_h = LV_MAX(max_h, lv_obj_get_height(stat_boxes[i]));
    for (int i = 0; i < 4; i++) lv_obj_set_height(stat_boxes[i], max_h);
}

/* ---- forecast strip ------------------------------------------------------ */

static void build_forecast_strip(lv_obj_t *parent) {
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_remove_style_all(section);
    lv_obj_set_size(section, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(section, 1);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(section, 10, 0);
    lv_obj_remove_flag(section, LV_OBJ_FLAG_SCROLLABLE);

    ui.forecast_title = lv_label_create(section);
    lv_label_set_text(ui.forecast_title, weather_strings[ui.lang].forecast);
    lv_obj_set_style_text_color(ui.forecast_title, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ui.forecast_title, FONT_12, 0);
    /* FIX: the design's h6 is letter-spaced 0.08em and sits 2px in. Its
     * uppercasing has no LVGL equivalent — see the deviation list. */
    lv_obj_set_style_text_letter_space(ui.forecast_title, 1, 0);
    lv_obj_set_style_pad_left(ui.forecast_title, 2, 0);

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
        /* FIX: the design has flex:1 with min-width 120px, so the seven cards
         * fill the row. Fixed 120 left 64px empty at the right edge. */
        lv_obj_set_width(card, 120);
        lv_obj_set_height(card, LV_PCT(100));
        lv_obj_set_flex_grow(card, 1);
        lv_obj_set_style_bg_color(card, C_SURFACE, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, C_DIVIDER, 0);
        lv_obj_set_style_radius(card, R_MD, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_style_pad_row(card, 8, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(card, day_card_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        ui.day_name_lbl[i] = lv_label_create(card);
        lv_label_set_text(ui.day_name_lbl[i], "");
        lv_obj_set_style_text_color(ui.day_name_lbl[i], C_TEXT, 0);
        lv_obj_set_style_text_font(ui.day_name_lbl[i], FONT_14, 0);
        ui.day_date_lbl[i] = lv_label_create(card);
        lv_label_set_text(ui.day_date_lbl[i], "");
        lv_obj_set_style_text_color(ui.day_date_lbl[i], C_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(ui.day_date_lbl[i], FONT_10, 0);

        /* FIX: same as ui.current_icon — the design's forecast tile sets
         * color:var(--color-text) on the whole card, no accent override. */
        ui.day_icons[i] = weather_icon_create(card, WX_ICON_CLOUD, 36, C_TEXT);

        lv_obj_t *hilo = lv_obj_create(card);
        lv_obj_remove_style_all(hilo);
        lv_obj_set_size(hilo, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(hilo, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(hilo, 6, 0);
        lv_obj_remove_flag(hilo, LV_OBJ_FLAG_SCROLLABLE);
        ui.day_hi_lbl[i] = lv_label_create(hilo);
        lv_label_set_text(ui.day_hi_lbl[i], "");
        lv_obj_set_style_text_color(ui.day_hi_lbl[i], C_TEXT, 0);
        lv_obj_set_style_text_font(ui.day_hi_lbl[i], FONT_16, 0);
        ui.day_lo_lbl[i] = lv_label_create(hilo);
        lv_label_set_text(ui.day_lo_lbl[i], "");
        lv_obj_set_style_text_color(ui.day_lo_lbl[i], C_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(ui.day_lo_lbl[i], FONT_14, 0);

        ui.day_precip_lbl[i] = lv_label_create(card);
        lv_label_set_text(ui.day_precip_lbl[i], "");
        lv_obj_set_style_text_color(ui.day_precip_lbl[i], C_ACCENT, 0);
        lv_obj_set_style_text_font(ui.day_precip_lbl[i], FONT_10, 0);
        /* FIX: the design wears this as .tag.tag-outline, not as bare text —
         * 1px accent border, radius-md * 0.75, padding 3px/10px. */
        lv_obj_set_style_border_width(ui.day_precip_lbl[i], 1, 0);
        lv_obj_set_style_border_color(ui.day_precip_lbl[i], C_ACCENT, 0);
        lv_obj_set_style_radius(ui.day_precip_lbl[i], 6, 0);
        lv_obj_set_style_pad_hor(ui.day_precip_lbl[i], 10, 0);
        lv_obj_set_style_pad_ver(ui.day_precip_lbl[i], 3, 0);

        add_event_bubble_recursive(card);
        ui.day_cards[i] = card;
    }
}

/* ---- render ------------------------------------------------------------- */

static void render_current(void) {
    if (!ui.has_data) return;
    lv_label_set_text_fmt(ui.header_location_lbl, "%s%s%s", ui.cur.location_name,
                           ui.cur.location_country[0] ? ", " : "", ui.cur.location_country);
    lv_label_set_text(ui.header_time_lbl, ui.cur.time_str);
    lv_label_set_text(ui.header_date_lbl, ui.cur.date_str);

    int bucket = wmo_bucket(ui.cur.weather_code);
    lv_label_set_text(ui.current_cond_tag, weather_cond_text[ui.lang][bucket]);
    weather_icon_set_type(ui.current_icon, weather_icon_from_wmo(ui.cur.weather_code), 72, C_TEXT);
    lv_label_set_text_fmt(ui.current_temp_lbl, "%d\xC2\xB0%c", temp_disp(ui.cur.temp_c), temp_unit_ch());
    lv_label_set_text(ui.current_feel_lbl, ui.cur.real_feel_text);

    lv_label_set_text(ui.stat_humidity_val, humidity_cat(ui.cur.humidity_pct));
    lv_label_set_text_fmt(ui.stat_humidity_sub, "%d%%", ui.cur.humidity_pct);
    lv_label_set_text_fmt(ui.stat_feel_val, "%d\xC2\xB0%c", temp_disp(ui.cur.feels_like_c), temp_unit_ch());
    lv_label_set_text_fmt(ui.stat_wind_val, "%d %s", (int)lroundf(wind_disp(ui.cur.wind_kmh)), wind_unit_str());
    lv_label_set_text_fmt(ui.stat_precip_val, "%d%%", ui.cur.precip_pct);

    /* Stat captions and the forecast heading are language-dependent too. */
    lv_label_set_text(ui.stat_humidity_cap, weather_strings[ui.lang].humidity);
    lv_label_set_text(ui.stat_feel_cap, weather_strings[ui.lang].feels_like);
    lv_label_set_text(ui.stat_wind_cap, weather_strings[ui.lang].wind);
    lv_label_set_text(ui.stat_precip_cap, weather_strings[ui.lang].precip);
    lv_label_set_text(ui.forecast_title, weather_strings[ui.lang].forecast);
    lv_label_set_text(ui.header_stale_lbl, weather_strings[ui.lang].data_outdated);
}

static void render_days(void) {
    if (!ui.days_valid) return;
    for (int i = 0; i < WEATHER_UI_DAYS; i++) {
        weather_day_t *d = &ui.days[i];
        lv_label_set_text(ui.day_name_lbl[i], d->day_label);
        lv_label_set_text(ui.day_date_lbl[i], d->date_label);
        weather_icon_set_type(ui.day_icons[i], weather_icon_from_wmo(d->weather_code), 36, C_TEXT);
        /* FIX: the design's tempStr() appends the unit — "28°C", not "28°". */
        lv_label_set_text_fmt(ui.day_hi_lbl[i], "%d\xC2\xB0%c", temp_disp(d->temp_max_c), temp_unit_ch());
        lv_label_set_text_fmt(ui.day_lo_lbl[i], "%d\xC2\xB0%c", temp_disp(d->temp_min_c), temp_unit_ch());
        lv_label_set_text_fmt(ui.day_precip_lbl[i], "%d%%", d->precip_pct);
    }
}

/* ---- day detail sliding panel ------------------------------------------- */

static void detail_close_cb(lv_event_t *e);

static void slide_anim_y_cb(void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, v); }
static void detail_hide_backdrop_cb(lv_anim_t *a) { LV_UNUSED(a); lv_obj_add_flag(ui.detail_backdrop, LV_OBJ_FLAG_HIDDEN); }

/* FIX: every lv_obj is clickable by LVGL default, so any icon or wrapper container
 * placed on top of a tappable area (a forecast tile, the day-detail sheet, ...)
 * swallows the tap instead of forwarding it to the ancestor that actually handles
 * LV_EVENT_CLICKED — the design has no such dead zones, since HTML bubbles clicks
 * from any child to a parent's onClick unless stopPropagation() is called, which
 * none of these do. Flagging a subtree with this makes it bubble the same way. */
static void add_event_bubble_recursive(lv_obj_t *obj) {
    uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        lv_obj_add_flag(child, LV_OBJ_FLAG_EVENT_BUBBLE);
        add_event_bubble_recursive(child);
    }
}

static void build_detail_panel(lv_obj_t *parent) {
    ui.detail_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.detail_backdrop);
    lv_obj_set_size(ui.detail_backdrop, LV_PCT(100), LV_PCT(100));
    /* FIX: ui.root is a column flex; a 100%-height sibling here competes with
     * content's flex_grow for the same space instead of overlaying the whole
     * screen, so once shown it landed pushed down (and clipped) by whatever
     * that tug-of-war left content with. IGNORE_LAYOUT takes it out of root's
     * flex entirely, back to sitting at its own explicit (0,0)/100%x100%. */
    lv_obj_add_flag(ui.detail_backdrop, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(ui.detail_backdrop, C_NEUTRAL_900, 0);   /* FIX: .dialog-backdrop is neutral-900 at 50 %, not black */
    lv_obj_set_style_bg_opa(ui.detail_backdrop, LV_OPA_50, 0);
    lv_obj_add_flag(ui.detail_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.detail_backdrop, detail_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(ui.detail_backdrop, LV_OBJ_FLAG_HIDDEN);

    ui.detail_panel = lv_obj_create(ui.detail_backdrop);
    lv_obj_remove_style_all(ui.detail_panel);
    lv_obj_set_size(ui.detail_panel, LV_PCT(100), DETAIL_H);
    lv_obj_align(ui.detail_panel, LV_ALIGN_BOTTOM_MID, 0, DETAIL_H); /* parked off-screen below */
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
    /* FIX: design aligns this row with align-items:center, not start. */
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    ui.detail_day_lbl = lv_label_create(head);
    lv_label_set_text(ui.detail_day_lbl, "");
    lv_obj_set_style_text_color(ui.detail_day_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.detail_day_lbl, FONT_22, 0);

    /* FIX: the export dropped the close (X) button the design has in this corner. */
    lv_obj_t *detail_close_btn = lv_obj_create(head);
    lv_obj_remove_style_all(detail_close_btn);
    lv_obj_set_size(detail_close_btn, 36, 36);
    lv_obj_add_flag(detail_close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(detail_close_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(detail_close_btn, detail_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *detail_close_lbl = lv_label_create(detail_close_btn);
    lv_label_set_text(detail_close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(detail_close_lbl, C_ACCENT, 0);
    lv_obj_set_style_text_font(detail_close_lbl, FONT_SYMBOL, 0);
    lv_obj_center(detail_close_lbl);

    lv_obj_t *body = lv_obj_create(ui.detail_panel);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(body, 20, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    /* FIX: same as ui.current_icon — no accent override in the design. */
    ui.detail_icon = weather_icon_create(body, WX_ICON_CLOUD, 64, C_TEXT);
    lv_obj_t *hilo_wrap = lv_obj_create(body);
    lv_obj_remove_style_all(hilo_wrap);
    lv_obj_set_size(hilo_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hilo_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(hilo_wrap, LV_OBJ_FLAG_SCROLLABLE);
    ui.detail_hilo_lbl = lv_label_create(hilo_wrap);
    lv_label_set_text(ui.detail_hilo_lbl, "");
    lv_obj_set_style_text_color(ui.detail_hilo_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.detail_hilo_lbl, FONT_36, 0);
    ui.detail_cond_lbl = lv_label_create(hilo_wrap);
    lv_label_set_text(ui.detail_cond_lbl, "");
    lv_obj_set_style_text_color(ui.detail_cond_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ui.detail_cond_lbl, FONT_14, 0);

    ui.detail_chart = weather_chart_create(ui.detail_panel, LV_PCT(100), 150);

    lv_obj_t *stats = lv_obj_create(ui.detail_panel);
    lv_obj_remove_style_all(stats);
    lv_obj_set_size(stats, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(stats, 12, 0);
    lv_obj_remove_flag(stats, LV_OBJ_FLAG_SCROLLABLE);
    stat_box_create(stats, weather_strings[ui.lang].feels_like, &ui.detail_feel_lbl, &ui.detail_feel_cap, NULL, 104, 10, false);
    stat_box_create(stats, weather_strings[ui.lang].precip, &ui.detail_precip_lbl, &ui.detail_precip_cap, NULL, 104, 10, false);
    stat_box_create(stats, weather_strings[ui.lang].wind, &ui.detail_wind_lbl, &ui.detail_wind_cap, NULL, 104, 10, false);

    add_event_bubble_recursive(ui.detail_panel);
}

static void detail_slide(bool open) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui.detail_panel);
    lv_anim_set_exec_cb(&a, slide_anim_y_cb);
    lv_anim_set_duration(&a, 220);
    if (open) {
        lv_obj_remove_flag(ui.detail_backdrop, LV_OBJ_FLAG_HIDDEN);
        lv_anim_set_values(&a, DETAIL_H, 0);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    } else {
        lv_anim_set_values(&a, 0, DETAIL_H);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        /* FIX: the export called lv_anim_set_completed_cb() *after* lv_anim_start(),
         * mutating a local copy the animation had already been created from — so the
         * backdrop stayed up (and kept swallowing touches) after closing. */
        lv_anim_set_completed_cb(&a, detail_hide_backdrop_cb);
    }
    lv_anim_start(&a);
}

static void detail_close_cb(lv_event_t *e) { LV_UNUSED(e); detail_slide(false); }

static void day_card_click_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= WEATHER_UI_DAYS || !ui.days_valid) return;
    weather_day_t *d = &ui.days[i];
    lv_label_set_text_fmt(ui.detail_day_lbl, "%s\n%s", d->day_label, d->date_label);
    weather_icon_set_type(ui.detail_icon, weather_icon_from_wmo(d->weather_code), 64, C_TEXT);
    lv_label_set_text_fmt(ui.detail_hilo_lbl, "%d\xC2\xB0 / %d\xC2\xB0", temp_disp(d->temp_max_c), temp_disp(d->temp_min_c));
    lv_label_set_text(ui.detail_cond_lbl, weather_cond_text[ui.lang][wmo_bucket(d->weather_code)]);
    lv_label_set_text(ui.detail_feel_cap, weather_strings[ui.lang].feels_like);
    lv_label_set_text(ui.detail_precip_cap, weather_strings[ui.lang].precip);
    lv_label_set_text(ui.detail_wind_cap, weather_strings[ui.lang].wind);
    lv_label_set_text_fmt(ui.detail_feel_lbl, "%d\xC2\xB0 / %d\xC2\xB0", temp_disp(d->feels_max_c), temp_disp(d->feels_min_c));
    lv_label_set_text_fmt(ui.detail_precip_lbl, "%d%%", d->precip_pct);
    lv_label_set_text_fmt(ui.detail_wind_lbl, "%d %s", (int)lroundf(wind_disp(d->wind_max_kmh)), wind_unit_str());
    detail_slide(true);
    /* After the backdrop is visible: a hidden parent is skipped by flex layout, so
     * measuring the chart before this point yields a zero-sized, blank plot. */
    lv_obj_update_layout(ui.detail_panel);
    weather_chart_set_data(ui.detail_chart, ui.hourly_day_valid[i] ? &ui.hourly_days[i] : NULL,
                           ui.temp_unit == WX_UNIT_F);
}

/* ---- search overlay (native on-screen keyboard, no physical keys needed) - */

static void search_ta_event_cb(lv_event_t *e) {
    LV_UNUSED(e);
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
    lv_label_set_text(ui.search_status_lbl, "");
    /* Captions follow the current language. */
    lv_textarea_set_placeholder_text(ui.search_ta, weather_strings[ui.lang].search_placeholder);
    lv_label_set_text(ui.search_cancel_lbl, weather_strings[ui.lang].cancel);
    lv_obj_remove_flag(ui.search_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(ui.search_ta, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(ui.search_kb, ui.search_ta);
}

/* ---- on-screen keyboard: umlauts ----------------------------------------
 *
 * LVGL's built-in TEXT_LOWER/TEXT_UPPER maps are plain US QWERTY with no
 * accented characters at all, so city names (search) and network names/
 * passwords (Wi-Fi) had no way to enter ä/ö/ü — everything typed on the
 * device itself was ASCII-only regardless of UI language. lv_keyboard_set_map()
 * replaces LVGL's own default maps process-wide (they're stored in a static
 * table keyed by mode, not per keyboard instance), so setting this once here
 * covers every lv_keyboard in the app, matching what the design's own on-
 * screen keyboard already does for its Wi-Fi screens (a dedicated Ä/Ö/Ü row).
 *
 * Structurally these are copies of LVGL's default_kb_map_lc/uc and their
 * shared ctrl map (lv_keyboard.c) — that file's tables are static, so they
 * can't be reused directly — with the row-3 punctuation trio (. , :) swapped
 * for ä/ö/ü (Ä/Ö/Ü uppercase). The control map's widths/flags are unchanged;
 * only three button labels differ from the LVGL defaults. */
static const char *const kb_map_lc_umlaut[] = {
    "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", "ä", "ö", "ü", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const char *const kb_map_uc_umlaut[] = {
    "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "Z", "X", "C", "V", "B", "N", "M", "Ä", "Ö", "Ü", "\n",
    LV_SYMBOL_CLOSE, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const lv_buttonmatrix_ctrl_t kb_ctrl_umlaut[] = {
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5,
    LV_BUTTONMATRIX_CTRL_POPOVER | 4, LV_BUTTONMATRIX_CTRL_POPOVER | 4, LV_BUTTONMATRIX_CTRL_POPOVER | 4,
    LV_BUTTONMATRIX_CTRL_POPOVER | 4, LV_BUTTONMATRIX_CTRL_POPOVER | 4, LV_BUTTONMATRIX_CTRL_POPOVER | 4,
    LV_BUTTONMATRIX_CTRL_POPOVER | 4, LV_BUTTONMATRIX_CTRL_POPOVER | 4, LV_BUTTONMATRIX_CTRL_POPOVER | 4,
    LV_BUTTONMATRIX_CTRL_POPOVER | 4,
    LV_BUTTONMATRIX_CTRL_CHECKED | 7,

    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6,
    LV_BUTTONMATRIX_CTRL_POPOVER | 3, LV_BUTTONMATRIX_CTRL_POPOVER | 3, LV_BUTTONMATRIX_CTRL_POPOVER | 3,
    LV_BUTTONMATRIX_CTRL_POPOVER | 3, LV_BUTTONMATRIX_CTRL_POPOVER | 3, LV_BUTTONMATRIX_CTRL_POPOVER | 3,
    LV_BUTTONMATRIX_CTRL_POPOVER | 3, LV_BUTTONMATRIX_CTRL_POPOVER | 3, LV_BUTTONMATRIX_CTRL_POPOVER | 3,
    LV_BUTTONMATRIX_CTRL_CHECKED | 7,

    LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_POPOVER | 1,
    LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_POPOVER | 1,
    LV_BUTTONMATRIX_CTRL_POPOVER | 1, LV_BUTTONMATRIX_CTRL_POPOVER | 1, LV_BUTTONMATRIX_CTRL_POPOVER | 1,
    LV_BUTTONMATRIX_CTRL_POPOVER | 1, LV_BUTTONMATRIX_CTRL_POPOVER | 1, LV_BUTTONMATRIX_CTRL_POPOVER | 1,
    LV_BUTTONMATRIX_CTRL_POPOVER | 1,
    LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_POPOVER | 1,
    LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_POPOVER | 1,
    LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_POPOVER | 1,

    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
    LV_BUTTONMATRIX_CTRL_CHECKED | 2,
    6,
    LV_BUTTONMATRIX_CTRL_CHECKED | 2,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2,
};

static void kb_add_umlauts(lv_obj_t *kb) {
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_lc_umlaut, kb_ctrl_umlaut);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_uc_umlaut, kb_ctrl_umlaut);
    /* The keyboard's default font is LVGL's global default (ASCII-only
     * Montserrat), which can't draw ä/ö/ü either. FONT_14 (Inter) can — and
     * now falls back to Montserrat (see tools/gen_fonts.sh) for the button
     * glyphs (backspace, enter, arrows, ok) Inter itself doesn't have. */
    lv_obj_set_style_text_font(kb, FONT_14, 0);
}

static void build_search_overlay(lv_obj_t *parent) {
    ui.search_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.search_backdrop);
    lv_obj_set_size(ui.search_backdrop, LV_PCT(100), LV_PCT(100));
    /* FIX: see the matching comment in build_detail_panel() — a 100%-height
     * sibling in root's column flex fights content's flex_grow for space instead
     * of overlaying the full screen. */
    lv_obj_add_flag(ui.search_backdrop, LV_OBJ_FLAG_IGNORE_LAYOUT);
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
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    ui.search_ta = lv_textarea_create(top);
    lv_textarea_set_one_line(ui.search_ta, true);
    lv_textarea_set_placeholder_text(ui.search_ta, weather_strings[ui.lang].search_placeholder);
    /* FIX: no explicit font fell back to LVGL's global default (ASCII-only
     * Montserrat) — typed ä/ö/ü (and pasted-in city names with them) showed as
     * tofu boxes. */
    lv_obj_set_style_text_font(ui.search_ta, FONT_14, 0);
    lv_obj_set_flex_grow(ui.search_ta, 1);
    lv_obj_add_event_cb(ui.search_ta, search_ta_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *cancel = lv_obj_create(top);
    lv_obj_remove_style_all(cancel);
    lv_obj_set_size(cancel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    /* Synced from Claude Design 2026-09-10: padding 8 -> 10/18, >=44px touch target. */
    lv_obj_set_style_min_height(cancel, 44, 0);
    lv_obj_set_style_pad_hor(cancel, 18, 0);
    lv_obj_set_style_pad_ver(cancel, 10, 0);
    lv_obj_set_style_border_width(cancel, 1, 0);
    lv_obj_set_style_border_color(cancel, C_DIVIDER, 0);
    lv_obj_set_style_radius(cancel, R_MD, 0);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cancel, search_cancel_cb, LV_EVENT_CLICKED, NULL);
    ui.search_cancel_lbl = lv_label_create(cancel);
    lv_label_set_text(ui.search_cancel_lbl, weather_strings[ui.lang].cancel);
    lv_obj_set_style_text_color(ui.search_cancel_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.search_cancel_lbl, FONT_14, 0);
    lv_obj_center(ui.search_cancel_lbl);

    ui.search_status_lbl = lv_label_create(ui.search_backdrop);
    lv_obj_set_style_text_color(ui.search_status_lbl, C_TEXT_MUTED, 0);
    /* FIX: never had an explicit font, so it fell back to LVGL's global default
     * (lv_font_montserrat_14, ASCII-only) instead of the Inter build — "Keine
     * Städte gefunden." rendered its ä as a tofu box. */
    lv_obj_set_style_text_font(ui.search_status_lbl, FONT_14, 0);
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
    kb_add_umlauts(ui.search_kb);
    lv_keyboard_set_mode(ui.search_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
}

void weather_ui_set_searching(bool searching) {
    if (!ui.search_status_lbl) return;
    lv_label_set_text(ui.search_status_lbl, searching ? weather_strings[ui.lang].searching : "");
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
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, search_result_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *n = lv_label_create(row);
        lv_label_set_text(n, names[i]);
        lv_obj_set_style_text_color(n, C_TEXT, 0);
        /* FIX: never had an explicit font, so it fell back to LVGL's global
         * default (lv_font_montserrat_14, ASCII-only) instead of the Inter
         * build — city names with ä/ö/ü/etc. (from the geocoding API, not our
         * own string tables) rendered those glyphs as tofu boxes. */
        lv_obj_set_style_text_font(n, FONT_14, 0);
        lv_obj_t *s = lv_label_create(row);
        lv_label_set_text(s, subs[i]);
        lv_obj_set_style_text_color(s, C_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(s, FONT_12, 0);
    }
}

/* ---- settings panel ------------------------------------------------------ */

static void settings_close_cb(lv_event_t *e) { LV_UNUSED(e); lv_obj_add_flag(ui.settings_backdrop, LV_OBJ_FLAG_HIDDEN); }
static void open_settings_cb(lv_event_t *e) { LV_UNUSED(e); lv_obj_remove_flag(ui.settings_backdrop, LV_OBJ_FLAG_HIDDEN); }

static void open_wifi_cb(lv_event_t *e) {
    LV_UNUSED(e);
    lv_obj_add_flag(ui.settings_backdrop, LV_OBJ_FLAG_HIDDEN);
    weather_ui_open_wifi_setup();
}

static void notify_settings_changed(void) {
    render_current();
    render_days();
    if (ui.today_chart)
        weather_chart_set_data(ui.today_chart, ui.hourly_today_valid ? &ui.hourly_today : NULL,
                               ui.temp_unit == WX_UNIT_F);
    if (ui.on_settings_changed) ui.on_settings_changed(ui.lang, ui.temp_unit, ui.wind_unit, ui.time_fmt);
}

/* FIX: the export left every segmented control except Language wired to a NULL
 * callback, so the °C/°F, km/h/mph/m/s and 24h/12h controls highlighted on tap but
 * changed nothing. All four are live now. */
static void on_seg_lang(int idx, void *user) { LV_UNUSED(user); weather_ui_set_language((weather_lang_t)idx); }
static void on_seg_temp(int idx, void *user) { LV_UNUSED(user); ui.temp_unit = (wx_temp_unit_t)idx; notify_settings_changed(); }
static void on_seg_wind(int idx, void *user) { LV_UNUSED(user); ui.wind_unit = (wx_wind_unit_t)idx; notify_settings_changed(); }
static void on_seg_time(int idx, void *user) { LV_UNUSED(user); ui.time_fmt = (wx_time_fmt_t)idx; notify_settings_changed(); }

/* Applied on every LV_EVENT_VALUE_CHANGED tick while dragging (so the backlight
 * follows the finger), and once more on LV_EVENT_RELEASED so the caller can
 * persist the settled value without writing NVS on every intermediate step. */
static void on_brightness_slider(lv_event_t *e) {
    ui.brightness = (int)lv_slider_get_value(ui.brightness_slider);
    if (ui.on_brightness) ui.on_brightness(ui.brightness, lv_event_get_code(e) == LV_EVENT_RELEASED);
    /* Manual adjustment wins over the sensor: dragging the slider while
     * adaptive mode is on turns it back off, same as tapping the switch
     * itself — the two controls would otherwise fight over the backlight. */
    if (ui.brightness_adaptive_sw && lv_obj_has_state(ui.brightness_adaptive_sw, LV_STATE_CHECKED)) {
        lv_obj_remove_state(ui.brightness_adaptive_sw, LV_STATE_CHECKED);
        ui.brightness_adaptive = false;
        if (ui.on_brightness_adaptive) ui.on_brightness_adaptive(false);
    }
}

static void on_brightness_adaptive_sw(lv_event_t *e) {
    LV_UNUSED(e);
    bool on = lv_obj_has_state(ui.brightness_adaptive_sw, LV_STATE_CHECKED);
    ui.brightness_adaptive = on;
    if (ui.on_brightness_adaptive) ui.on_brightness_adaptive(on);
}

void weather_ui_set_callbacks(weather_ui_search_cb_t on_search, weather_ui_select_city_cb_t on_select_city,
                               weather_ui_refresh_cb_t on_refresh, weather_ui_settings_changed_cb_t on_settings_changed) {
    ui.on_search = on_search;
    ui.on_select_city = on_select_city;
    ui.on_refresh = on_refresh;
    ui.on_settings_changed = on_settings_changed;
}

weather_lang_t weather_ui_get_language(void) { return ui.lang; }
wx_time_fmt_t  weather_ui_get_time_fmt(void) { return ui.time_fmt; }

/* Rebuilding settings makes it the topmost child; restore the overlay stacking. */
static void settings_rebuild(bool keep_open) {
    if (!ui.settings_backdrop) return;
    lv_obj_delete(ui.settings_backdrop);
    build_settings_panel(ui.root);
    if (keep_open) lv_obj_remove_flag(ui.settings_backdrop, LV_OBJ_FLAG_HIDDEN);
    if (ui.search_backdrop) lv_obj_move_foreground(ui.search_backdrop);
    if (ui.detail_backdrop) lv_obj_move_foreground(ui.detail_backdrop);
    if (ui.wifi_backdrop)   lv_obj_move_foreground(ui.wifi_backdrop);
    /* Device info stacks on top of settings (see the "device info dialog"
     * comment above build_device_info_panel) — without this a rebuild here
     * makes settings the newest (topmost) sibling and buries it underneath. */
    if (ui.device_info_backdrop) lv_obj_move_foreground(ui.device_info_backdrop);
}

void weather_ui_set_language(weather_lang_t lang) {
    if (lang < 0 || lang >= LANG_COUNT) return;
    bool was_open = ui.settings_backdrop && !lv_obj_has_flag(ui.settings_backdrop, LV_OBJ_FLAG_HIDDEN);
    bool device_info_was_open = ui.device_info_backdrop && !lv_obj_has_flag(ui.device_info_backdrop, LV_OBJ_FLAG_HIDDEN);
    ui.lang = lang;
    /* FIX: the export's README listed re-localizing the settings panel's own captions
     * as a TODO — the panel built its labels once, so switching to e.g. German left
     * "Language / Temperature / Wind speed" in English. Rebuild it in place. */
    settings_rebuild(was_open);
    device_info_rebuild(device_info_was_open);
    wifi_relabel();
    notify_settings_changed();
}

void weather_ui_set_units(wx_temp_unit_t temp, wx_wind_unit_t wind, wx_time_fmt_t time_fmt) {
    bool was_open = ui.settings_backdrop && !lv_obj_has_flag(ui.settings_backdrop, LV_OBJ_FLAG_HIDDEN);
    ui.temp_unit = temp; ui.wind_unit = wind; ui.time_fmt = time_fmt;
    settings_rebuild(was_open);
    notify_settings_changed();
}

void weather_ui_set_brightness(int percent) {
    ui.brightness = percent;
    if (ui.brightness_slider) lv_slider_set_value(ui.brightness_slider, percent, LV_ANIM_OFF);
}

void weather_ui_set_brightness_adaptive(bool on) {
    ui.brightness_adaptive = on;
    if (!ui.brightness_adaptive_sw) return;
    if (on) lv_obj_add_state(ui.brightness_adaptive_sw, LV_STATE_CHECKED);
    else lv_obj_remove_state(ui.brightness_adaptive_sw, LV_STATE_CHECKED);
}

void weather_ui_set_brightness_adaptive_available(bool available) {
    ui.brightness_adaptive_available = available;
    if (!available) ui.brightness_adaptive = false;
    if (!ui.brightness_adaptive_sw) return;
    if (available) {
        lv_obj_remove_state(ui.brightness_adaptive_sw, LV_STATE_DISABLED);
    } else {
        /* FIX: this only grayed the switch out, it never actually cleared
         * LV_STATE_CHECKED — contradicting this function's own "forces it
         * visually off" doc comment. It happened to look right anyway
         * because every caller today follows this with
         * weather_ui_set_brightness_adaptive(false), but a future caller
         * relying on the documented guarantee alone would get a dimmed
         * switch stuck showing checked. */
        lv_obj_add_state(ui.brightness_adaptive_sw, LV_STATE_DISABLED);
        lv_obj_remove_state(ui.brightness_adaptive_sw, LV_STATE_CHECKED);
    }
}

void weather_ui_set_current(const weather_current_t *cur) { ui.cur = *cur; ui.has_data = true; render_current(); }

void weather_ui_set_days(const weather_day_t days[WEATHER_UI_DAYS]) {
    memcpy(ui.days, days, sizeof(ui.days));
    ui.days_valid = true;
    render_days();
}

/* FIX: the export gated this on ui.loading_lbl, a field that was declared but never
 * assigned — so it was always NULL and the whole function was a no-op. */
void weather_ui_set_loading(bool loading) {
    if (!ui.current_card) return;
    lv_obj_set_style_opa(ui.current_card, (loading && !ui.has_data) ? LV_OPA_40 : LV_OPA_COVER, 0);
    if (loading && !ui.has_data) lv_label_set_text(ui.current_feel_lbl, weather_strings[ui.lang].loading);
}

void weather_ui_set_error(const char *msg_or_null) {
    if (!ui.error_bar) return;
    if (msg_or_null) {
        lv_label_set_text(ui.error_lbl, msg_or_null);
        lv_label_set_text(ui.error_retry_lbl, weather_strings[ui.lang].retry);
        lv_obj_remove_flag(ui.error_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.error_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_settings_panel(lv_obj_t *parent) {
    ui.settings_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.settings_backdrop);
    lv_obj_set_size(ui.settings_backdrop, LV_PCT(100), LV_PCT(100));
    /* FIX: see the matching comment in build_detail_panel() — without this, a
     * 100%-height sibling in root's column flex fights content's flex_grow for
     * space instead of overlaying the full screen, and once shown it landed
     * pushed down (and the dialog inside it miscentered) by whatever that
     * tug-of-war left content with. This is why the settings dialog wasn't
     * actually centered on screen despite lv_obj_center(). */
    lv_obj_add_flag(ui.settings_backdrop, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(ui.settings_backdrop, C_NEUTRAL_900, 0);  /* FIX: same backdrop token as the detail panel */
    lv_obj_set_style_bg_opa(ui.settings_backdrop, LV_OPA_50, 0);
    lv_obj_add_flag(ui.settings_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.settings_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.settings_backdrop, settings_close_cb, LV_EVENT_CLICKED, NULL);

    ui.settings_panel = lv_obj_create(ui.settings_backdrop);
    lv_obj_remove_style_all(ui.settings_panel);
    lv_obj_set_size(ui.settings_panel, 380, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(ui.settings_panel, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui.settings_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.settings_panel, R_LG, 0);
    lv_obj_set_style_pad_all(ui.settings_panel, 20, 0);
    lv_obj_set_flex_flow(ui.settings_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.settings_panel, 14, 0);
    lv_obj_add_flag(ui.settings_panel, LV_OBJ_FLAG_CLICKABLE); /* swallow clicks so they don't bubble to the backdrop */

    /* FIX: the design puts the title and a ghost close button in one row.
     * The port had no way out of this panel except tapping the backdrop. */
    lv_obj_t *title_row = lv_obj_create(ui.settings_panel);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(title_row);
    lv_label_set_text(title, weather_strings[ui.lang].settings);
    lv_obj_set_style_text_color(title, C_TEXT, 0);
    lv_obj_set_style_text_font(title, FONT_20, 0);

    /* Synced from Claude Design 2026-09-10: a new info button appeared next to the
     * close button (opens the Device information dialog below), wrapped with it in
     * its own row so the pair stays grouped against title_row's space-between. Also
     * grew both buttons 44x44 -> 56x56 in the same pull, matching the header icons'
     * earlier bump. */
    lv_obj_t *btn_group = lv_obj_create(title_row);
    lv_obj_remove_style_all(btn_group);
    lv_obj_set_size(btn_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_group, 8, 0);
    lv_obj_remove_flag(btn_group, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *info_btn = lv_obj_create(btn_group);
    lv_obj_remove_style_all(info_btn);
    lv_obj_set_size(info_btn, 56, 56);
    lv_obj_add_flag(info_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(info_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(info_btn, open_device_info_cb, LV_EVENT_CLICKED, NULL);
    /* The design's info glyph is a filled circle with a cut-out "i" (single SVG
     * path, currentColor) — LVGL's built-in symbol font has no equivalent, so
     * it's composed from a small circle + label instead of a font glyph, same
     * idea as weather_icons.c's hand-drawn condition glyphs. */
    lv_obj_t *info_circle = lv_obj_create(info_btn);
    lv_obj_remove_style_all(info_circle);
    lv_obj_set_size(info_circle, 24, 24);
    lv_obj_set_style_radius(info_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(info_circle, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(info_circle, LV_OPA_COVER, 0);
    lv_obj_remove_flag(info_circle, LV_OBJ_FLAG_SCROLLABLE);
    /* FIX: lv_obj_create() defaults to CLICKABLE. Left on, this purely
     * decorative inner circle intercepted the tap before it reached
     * info_btn's own handler — found via the simulator's click-by-label-text
     * helper (sim/main.c), which walks up to the first clickable ancestor
     * exactly like real touch input would target the deepest clickable
     * object under the point. */
    lv_obj_remove_flag(info_circle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(info_circle);
    lv_obj_t *info_lbl = lv_label_create(info_circle);
    lv_label_set_text(info_lbl, "i");
    lv_obj_set_style_text_color(info_lbl, C_BG, 0);
    lv_obj_set_style_text_font(info_lbl, FONT_14, 0);
    lv_obj_center(info_lbl);

    lv_obj_t *close_btn = lv_obj_create(btn_group);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 56, 56);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(close_btn, settings_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, C_ACCENT, 0);
    lv_obj_set_style_text_font(close_lbl, FONT_SYMBOL, 0);
    lv_obj_center(close_lbl);

    /* Order must match weather_lang_t (EN, DE, ES, FR). The export listed
     * Deutsch first while LANG_EN == 0, so picking "Deutsch" selected English. */
    static const char *const lang_labels[4] = { "English", "Deutsch", "Espanol", "Francais" };
    lv_obj_t *f1 = field_wrap(ui.settings_panel, weather_strings[ui.lang].language);
    seg_create(f1, lang_labels, 4, ui.lang, on_seg_lang, NULL, ui.seg_lang);

    static const char *const temp_labels[2] = { "\xC2\xB0" "C", "\xC2\xB0" "F" };
    lv_obj_t *f2 = field_wrap(ui.settings_panel, weather_strings[ui.lang].temperature);
    seg_create(f2, temp_labels, 2, ui.temp_unit, on_seg_temp, NULL, ui.seg_temp);

    static const char *const wind_labels[3] = { "km/h", "mph", "m/s" };
    lv_obj_t *f3 = field_wrap(ui.settings_panel, weather_strings[ui.lang].wind_speed);
    seg_create(f3, wind_labels, 3, ui.wind_unit, on_seg_wind, NULL, ui.seg_wind);

    lv_obj_t *f4 = field_wrap(ui.settings_panel, weather_strings[ui.lang].time_format);
    const char *time_labels[2] = { weather_strings[ui.lang].h24, weather_strings[ui.lang].h12 };
    seg_create(f4, time_labels, 2, ui.time_fmt, on_seg_time, NULL, ui.seg_time);

    /* Synced from Claude Design 2026-09-09 (the "Weather app with 7-day forecast"
     * project's settingsOpen field grew a brightness slider), with the design's
     * "Adapt to ambient light" toggle added below it now that app_light.c gives
     * it something real to drive (the optional OV5647 on the MIPI-CSI connector). */
    lv_obj_t *f5 = field_wrap(ui.settings_panel, weather_strings[ui.lang].brightness);
    /* FIX: the knob sits half outside the track at each end (it's centered on the
     * value position, which reaches the track's own edge at min/max) — without this
     * inset it got clipped by the field's bounds at full brightness. */
    lv_obj_t *slider_wrap = lv_obj_create(f5);
    lv_obj_remove_style_all(slider_wrap);
    lv_obj_set_size(slider_wrap, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(slider_wrap, 8, 0);
    /* Synced from Claude Design 2026-09-10: 14px vertical padding around the
     * slider, widening its touch area beyond the thin track itself. */
    lv_obj_set_style_pad_ver(slider_wrap, 14, 0);
    lv_obj_remove_flag(slider_wrap, LV_OBJ_FLAG_SCROLLABLE);
    ui.brightness_slider = lv_slider_create(slider_wrap);
    lv_obj_set_width(ui.brightness_slider, LV_PCT(100));
    lv_slider_set_range(ui.brightness_slider, 10, 100);
    lv_slider_set_value(ui.brightness_slider, ui.brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui.brightness_slider, C_NEUTRAL_800, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui.brightness_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.brightness_slider, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ui.brightness_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui.brightness_slider, C_ACCENT_100, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(ui.brightness_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_add_event_cb(ui.brightness_slider, on_brightness_slider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui.brightness_slider, on_brightness_slider, LV_EVENT_RELEASED, NULL);

    lv_obj_t *adaptive_row = lv_obj_create(f5);
    lv_obj_remove_style_all(adaptive_row);
    lv_obj_set_size(adaptive_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(adaptive_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(adaptive_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(adaptive_row, 8, 0);
    lv_obj_remove_flag(adaptive_row, LV_OBJ_FLAG_SCROLLABLE);

    ui.brightness_adaptive_sw = lv_switch_create(adaptive_row);
    /* Matches the Nocturne switch tokens exactly (switchTrackStyle /
     * switchThumbStyle in the design source), not LVGL's built-in default
     * theme. FIX: the indicator and border previously had no distinct
     * unchecked-state color, so LVGL's own default-theme fill/border showed
     * through behind the knob and on the track outline instead of the
     * design's neutral tones. */
    lv_obj_set_style_bg_color(ui.brightness_adaptive_sw, C_NEUTRAL_800, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui.brightness_adaptive_sw, C_NEUTRAL_600, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui.brightness_adaptive_sw, C_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(ui.brightness_adaptive_sw, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.brightness_adaptive_sw, C_NEUTRAL_800, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui.brightness_adaptive_sw, C_ACCENT_700, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(ui.brightness_adaptive_sw, C_NEUTRAL_300, LV_PART_KNOB);
    lv_obj_set_style_bg_color(ui.brightness_adaptive_sw, C_ACCENT_100, LV_PART_KNOB | LV_STATE_CHECKED);
    /* No lv_theme is installed (everything here is styled from scratch), so
     * LV_STATE_DISABLED has no visual effect of its own — spell it out so a
     * board with no camera fitted actually looks non-interactive, not just
     * silently ignore taps. */
    lv_obj_set_style_opa(ui.brightness_adaptive_sw, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_event_cb(ui.brightness_adaptive_sw, on_brightness_adaptive_sw, LV_EVENT_VALUE_CHANGED, NULL);
    if (!ui.brightness_adaptive_available) lv_obj_add_state(ui.brightness_adaptive_sw, LV_STATE_DISABLED);
    /* FIX: settings_rebuild() (language/unit changes) tears down and recreates
     * this whole panel, and a freshly created switch always starts unchecked
     * — without this, changing the language silently turned adaptive
     * brightness back off, same as the brightness slider's value would reset
     * to 100 without the lv_slider_set_value() call below. */
    if (ui.brightness_adaptive) lv_obj_add_state(ui.brightness_adaptive_sw, LV_STATE_CHECKED);

    lv_obj_t *adaptive_lbl = lv_label_create(adaptive_row);
    lv_label_set_text(adaptive_lbl, weather_strings[ui.lang].brightness_adaptive);
    lv_obj_set_style_text_color(adaptive_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(adaptive_lbl, FONT_14, 0);

    /* Network row — the entry point into the on-device Wi-Fi setup the design added. */
    lv_obj_t *fnet = field_wrap(ui.settings_panel, weather_strings[ui.lang].network);
    lv_obj_t *net_btn = lv_obj_create(fnet);
    lv_obj_remove_style_all(net_btn);
    /* Synced from Claude Design 2026-09-10: 36 -> 44px touch target. */
    lv_obj_set_size(net_btn, LV_PCT(100), 44);
    lv_obj_set_style_border_width(net_btn, 1, 0);
    lv_obj_set_style_border_color(net_btn, C_DIVIDER, 0);
    lv_obj_set_style_radius(net_btn, R_MD, 0);
    lv_obj_add_flag(net_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(net_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(net_btn, open_wifi_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *net_btn_lbl = lv_label_create(net_btn);
    lv_label_set_text(net_btn_lbl, weather_strings[ui.lang].configure_network);
    lv_obj_set_style_text_color(net_btn_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(net_btn_lbl, FONT_14, 0);
    lv_obj_center(net_btn_lbl);

    /* FIX: centering right after creating settings_panel used its pre-children
     * SIZE_CONTENT height (near 0), so it centered against a sliver and then grew
     * downward as fields were added — the panel looked pinned near the top instead
     * of centered. Center only now that every field/row above exists and the
     * layout pass has resolved the panel's real height. */
    lv_obj_update_layout(ui.settings_panel);
    lv_obj_center(ui.settings_panel);
}

/* ---- device info dialog ----------------------------------------------------
 *
 * Synced from Claude Design 2026-09-10: a "Device information" dialog opens
 * from the new info button in the Settings title row. It stacks on top of
 * Settings rather than replacing it (the design's openDeviceInfo doesn't
 * touch settingsOpen), same relationship the Wi-Fi screen would have if it
 * didn't intentionally close Settings first. */

static void open_device_info_cb(lv_event_t *e) {
    LV_UNUSED(e);
    lv_obj_remove_flag(ui.device_info_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.device_info_backdrop);
}
static void close_device_info_cb(lv_event_t *e) { LV_UNUSED(e); lv_obj_add_flag(ui.device_info_backdrop, LV_OBJ_FLAG_HIDDEN); }

static void build_device_info_panel(lv_obj_t *parent) {
    const weather_strings_t *s = &weather_strings[ui.lang];

    ui.device_info_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.device_info_backdrop);
    lv_obj_set_size(ui.device_info_backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(ui.device_info_backdrop, LV_OBJ_FLAG_IGNORE_LAYOUT);  /* see build_settings_panel's FIX comment */
    lv_obj_set_style_bg_color(ui.device_info_backdrop, C_NEUTRAL_900, 0);
    lv_obj_set_style_bg_opa(ui.device_info_backdrop, LV_OPA_50, 0);
    lv_obj_add_flag(ui.device_info_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.device_info_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.device_info_backdrop, close_device_info_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(ui.device_info_backdrop);
    lv_obj_remove_style_all(panel);
    /* Design fixes 326x383 regardless of online/offline state, so the dialog
     * doesn't resize/jump when connectivity changes while it's open. */
    lv_obj_set_size(panel, 326, 383);
    lv_obj_set_style_bg_color(panel, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, R_LG, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title_row = lv_obj_create(panel);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(title_row);
    lv_label_set_text(title, s->device_info);
    lv_obj_set_style_text_color(title, C_TEXT, 0);
    lv_obj_set_style_text_font(title, FONT_20, 0);

    lv_obj_t *close_btn = lv_obj_create(title_row);
    lv_obj_remove_style_all(close_btn);
    lv_obj_set_size(close_btn, 56, 56);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(close_btn, close_device_info_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, C_ACCENT, 0);
    lv_obj_set_style_text_font(close_lbl, FONT_SYMBOL, 0);
    lv_obj_center(close_lbl);

    ui.device_info_name_val = info_row_create(panel, s->device_name);
    ui.device_info_hw_val   = info_row_create(panel, s->hardware_version);
    ui.device_info_fw_val   = info_row_create(panel, s->firmware_version);

    ui.device_info_online_group = lv_obj_create(panel);
    lv_obj_remove_style_all(ui.device_info_online_group);
    lv_obj_set_size(ui.device_info_online_group, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ui.device_info_online_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.device_info_online_group, 12, 0);
    lv_obj_remove_flag(ui.device_info_online_group, LV_OBJ_FLAG_SCROLLABLE);
    ui.device_info_ip_val = info_row_create(ui.device_info_online_group, s->ip_address);
    ui.device_info_dns_val = info_row_create(ui.device_info_online_group, s->dns);
    ui.device_info_gw_val = info_row_create(ui.device_info_online_group, s->gateway);
    ui.device_info_note_lbl = lv_label_create(ui.device_info_online_group);
    lv_obj_set_style_text_color(ui.device_info_note_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.device_info_note_lbl, FONT_14, 0);
    lv_label_set_long_mode(ui.device_info_note_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui.device_info_note_lbl, LV_PCT(100));

    ui.device_info_offline_lbl = lv_label_create(panel);
    lv_label_set_text(ui.device_info_offline_lbl, s->not_connected);
    lv_obj_set_style_text_color(ui.device_info_offline_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.device_info_offline_lbl, FONT_14, 0);

    /* Fill in whatever weather_ui_set_device_info() was last called with —
     * needed after a language-change rebuild, where the panel is torn down
     * and recreated from scratch but the underlying data hasn't changed. */
    if (ui.has_device_info) {
        lv_label_set_text(ui.device_info_name_val, ui.device_info_name);
        lv_label_set_text(ui.device_info_hw_val, ui.device_info_hw);
        lv_label_set_text(ui.device_info_fw_val, ui.device_info_fw);
        lv_label_set_text(ui.device_info_ip_val, ui.device_info_ip);
        lv_label_set_text(ui.device_info_dns_val, ui.device_info_dns);
        lv_label_set_text(ui.device_info_gw_val, ui.device_info_gw);
        lv_label_set_text(ui.device_info_note_lbl, ui.device_info_note);
    }
    if (ui.device_info_online) {
        lv_obj_remove_flag(ui.device_info_online_group, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.device_info_offline_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.device_info_online_group, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui.device_info_offline_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_update_layout(panel);   /* see build_settings_panel's matching FIX comment */
    lv_obj_center(panel);
}

/* Rebuilding makes it the topmost child; restore stacking like settings_rebuild(). */
static void device_info_rebuild(bool keep_open) {
    if (!ui.device_info_backdrop) return;
    lv_obj_delete(ui.device_info_backdrop);
    build_device_info_panel(ui.root);
    if (keep_open) lv_obj_remove_flag(ui.device_info_backdrop, LV_OBJ_FLAG_HIDDEN);
    if (ui.search_backdrop) lv_obj_move_foreground(ui.search_backdrop);
    if (ui.detail_backdrop) lv_obj_move_foreground(ui.detail_backdrop);
    if (ui.wifi_backdrop)   lv_obj_move_foreground(ui.wifi_backdrop);
}

void weather_ui_set_device_info(const weather_device_info_t *info) {
    if (!info) return;
    ui.has_device_info = true;
    snprintf(ui.device_info_name, sizeof ui.device_info_name, "%s", info->device_name ? info->device_name : "");
    snprintf(ui.device_info_hw, sizeof ui.device_info_hw, "%s", info->hardware_version ? info->hardware_version : "");
    snprintf(ui.device_info_fw, sizeof ui.device_info_fw, "%s", info->firmware_version ? info->firmware_version : "");
    ui.device_info_online = info->online;
    snprintf(ui.device_info_ip, sizeof ui.device_info_ip, "%s", info->ip ? info->ip : "");
    snprintf(ui.device_info_dns, sizeof ui.device_info_dns, "%s", info->dns ? info->dns : "");
    snprintf(ui.device_info_gw, sizeof ui.device_info_gw, "%s", info->gateway ? info->gateway : "");
    snprintf(ui.device_info_note, sizeof ui.device_info_note, "%s", info->note ? info->note : "");

    if (!ui.device_info_backdrop) return;   /* not built yet; weather_ui_create() picks this up */
    lv_label_set_text(ui.device_info_name_val, ui.device_info_name);
    lv_label_set_text(ui.device_info_hw_val, ui.device_info_hw);
    lv_label_set_text(ui.device_info_fw_val, ui.device_info_fw);
    lv_label_set_text(ui.device_info_ip_val, ui.device_info_ip);
    lv_label_set_text(ui.device_info_dns_val, ui.device_info_dns);
    lv_label_set_text(ui.device_info_gw_val, ui.device_info_gw);
    lv_label_set_text(ui.device_info_note_lbl, ui.device_info_note);
    if (ui.device_info_online) {
        lv_obj_remove_flag(ui.device_info_online_group, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.device_info_offline_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.device_info_online_group, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui.device_info_offline_lbl, LV_OBJ_FLAG_HIDDEN);
    }
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
    lv_obj_set_style_margin_hor(ui.error_bar, 24, 0);
    lv_obj_set_flex_flow(ui.error_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.error_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(ui.error_bar, LV_OBJ_FLAG_HIDDEN);
    ui.error_lbl = lv_label_create(ui.error_bar);
    lv_label_set_text(ui.error_lbl, "");
    lv_obj_set_style_text_color(ui.error_lbl, C_TEXT, 0);
    /* FIX: never had an explicit font — same class of bug as search_status_lbl
     * and the search result name label above. */
    lv_obj_set_style_text_font(ui.error_lbl, FONT_14, 0);
    lv_obj_t *retry = lv_obj_create(ui.error_bar);
    lv_obj_remove_style_all(retry);
    lv_obj_set_size(retry, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    /* Synced from Claude Design 2026-09-10: padding 6 -> 10/18, >=44px touch target. */
    lv_obj_set_style_min_height(retry, 44, 0);
    lv_obj_set_style_pad_hor(retry, 18, 0);
    lv_obj_set_style_pad_ver(retry, 10, 0);
    lv_obj_set_style_border_width(retry, 1, 0);
    lv_obj_set_style_border_color(retry, C_DIVIDER, 0);
    lv_obj_set_style_radius(retry, R_MD, 0);
    lv_obj_add_flag(retry, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(retry, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(retry, refresh_click_cb, LV_EVENT_CLICKED, NULL);
    ui.error_retry_lbl = lv_label_create(retry);
    lv_label_set_text(ui.error_retry_lbl, weather_strings[ui.lang].retry);
    lv_obj_set_style_text_color(ui.error_retry_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.error_retry_lbl, FONT_14, 0);
    lv_obj_center(ui.error_retry_lbl);
}

/* ---- refresh toast ---------------------------------------------------------
 *
 * Synced from Claude Design 2026-09-10: tapping the header refresh icon (or
 * the error bar's retry button, which shares the same on_refresh callback)
 * now shows a small toast — "Weather data updated." on success, auto-
 * dismissing after 1s, or the existing error message on failure, staying
 * until tapped away. */

static void refresh_toast_hide_cb(lv_timer_t *t) {
    LV_UNUSED(t);
    ui.refresh_toast_timer = NULL;
    lv_obj_add_flag(ui.refresh_toast, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_toast_dismiss_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (ui.refresh_toast_timer) { lv_timer_delete(ui.refresh_toast_timer); ui.refresh_toast_timer = NULL; }
    lv_obj_add_flag(ui.refresh_toast, LV_OBJ_FLAG_HIDDEN);
}

static void build_refresh_toast(lv_obj_t *parent) {
    ui.refresh_toast = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.refresh_toast);
    lv_obj_set_size(ui.refresh_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(ui.refresh_toast, 520, 0);
    lv_obj_set_style_bg_color(ui.refresh_toast, C_SURFACE, 0);
    lv_obj_set_style_bg_opa(ui.refresh_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui.refresh_toast, 1, 0);
    lv_obj_set_style_border_color(ui.refresh_toast, C_DIVIDER, 0);
    lv_obj_set_style_radius(ui.refresh_toast, R_LG, 0);
    lv_obj_set_style_pad_hor(ui.refresh_toast, 20, 0);
    lv_obj_set_style_pad_ver(ui.refresh_toast, 14, 0);
    lv_obj_add_flag(ui.refresh_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ui.refresh_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ui.refresh_toast, refresh_toast_dismiss_cb, LV_EVENT_CLICKED, NULL);
    /* Floats above the whole screen regardless of which panel is open, same as
     * the dialog backdrops — see the matching comment in build_detail_panel(). */
    lv_obj_add_flag(ui.refresh_toast, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(ui.refresh_toast, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_add_flag(ui.refresh_toast, LV_OBJ_FLAG_HIDDEN);

    ui.refresh_toast_lbl = lv_label_create(ui.refresh_toast);
    lv_label_set_text(ui.refresh_toast_lbl, "");
    lv_obj_set_style_text_color(ui.refresh_toast_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.refresh_toast_lbl, FONT_14, 0);
}

void weather_ui_show_refresh_toast(bool ok) {
    if (!ui.refresh_toast) return;
    if (ui.refresh_toast_timer) { lv_timer_delete(ui.refresh_toast_timer); ui.refresh_toast_timer = NULL; }
    lv_label_set_text(ui.refresh_toast_lbl, ok ? weather_strings[ui.lang].data_updated : weather_strings[ui.lang].error);
    lv_obj_set_style_border_color(ui.refresh_toast, ok ? C_DIVIDER : C_ACCENT, 0);
    lv_obj_remove_flag(ui.refresh_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.refresh_toast);
    /* Success dismisses itself; an error stays until the user taps it away —
     * same asymmetry as the design's own refreshToastTimer, which is only
     * ever set in the success branch of refresh(). */
    if (ok) {
        ui.refresh_toast_timer = lv_timer_create(refresh_toast_hide_cb, 1000, NULL);
        lv_timer_set_repeat_count(ui.refresh_toast_timer, 1);
    }
}


/* ---- Wi-Fi setup screen --------------------------------------------------
 *
 * Added for the design's weather_ui_set_wifi_* contract: a touch-only device needs
 * to be joinable to a network without a serial cable, so this is a full-screen
 * two-view flow — a scan list, then a password pad on the built-in keyboard —
 * plus a manual-SSID path for hidden networks.
 */

static void wifi_close_cb(lv_event_t *e) { LV_UNUSED(e); lv_obj_add_flag(ui.wifi_backdrop, LV_OBJ_FLAG_HIDDEN); }
static void wifi_back_to_list_cb(lv_event_t *e) { LV_UNUSED(e); wifi_show_list(); }
static void wifi_forget_cb(lv_event_t *e) { LV_UNUSED(e); if (ui.on_wifi_forget) ui.on_wifi_forget(); }

static void wifi_show_list(void) {
    ui.wifi_manual_mode = false;
    lv_obj_remove_flag(ui.wifi_list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.wifi_pw_view, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui.wifi_title_lbl, weather_strings[ui.lang].wifi_title);
}

static void wifi_show_password(const char *ssid) {
    /* Leaving manual mode set here would make the next Connect press re-read the
     * password as a fresh SSID. */
    ui.wifi_manual_mode = false;
    snprintf(ui.wifi_pending_ssid, sizeof ui.wifi_pending_ssid, "%s", ssid ? ssid : "");
    lv_obj_add_flag(ui.wifi_list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.wifi_pw_view, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(ui.wifi_pw_ta, "");
    lv_textarea_set_password_mode(ui.wifi_pw_ta, true);
    lv_label_set_text(ui.wifi_pw_show_lbl, weather_strings[ui.lang].show);
    lv_label_set_text_fmt(ui.wifi_pw_prompt, "%s %s",
                          weather_strings[ui.lang].enter_password, ui.wifi_pending_ssid);
    lv_label_set_text(ui.wifi_status_lbl, "");
    lv_keyboard_set_mode(ui.wifi_pw_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(ui.wifi_pw_kb, ui.wifi_pw_ta);
}

/* FIX: build_wifi_screen() set every label from weather_strings[ui.lang] only once,
 * at build time — switching the language in Settings left the wifi setup's own
 * captions (Back, Enter network name manually, ...) stuck in whichever language was
 * active at boot. weather_ui_set_language() now calls this the same way it rebuilds
 * the settings panel. `wifi_manual_mode` tells the SSID prompt/Connect button apart
 * from the password one, matching wifi_manual_cb()/wifi_show_password() below. */
static void wifi_relabel(void) {
    if (!ui.wifi_backdrop) return;
    lv_label_set_text(ui.wifi_title_lbl, weather_strings[ui.lang].wifi_title);
    lv_label_set_text(ui.wifi_back_lbl, weather_strings[ui.lang].back);
    lv_label_set_text(ui.wifi_scan_btn_lbl, weather_strings[ui.lang].scan);
    lv_label_set_text(ui.wifi_manual_btn_lbl, weather_strings[ui.lang].enter_manually);
    lv_label_set_text(ui.wifi_forget_btn_lbl, weather_strings[ui.lang].forget_network);
    lv_label_set_text(ui.wifi_back2_lbl, weather_strings[ui.lang].back);
    lv_label_set_text(ui.wifi_pw_show_lbl, lv_textarea_get_password_mode(ui.wifi_pw_ta)
                       ? weather_strings[ui.lang].show : weather_strings[ui.lang].hide);
    if (ui.wifi_manual_mode) {
        lv_label_set_text(ui.wifi_pw_prompt, weather_strings[ui.lang].enter_ssid);
        lv_label_set_text(ui.wifi_connect_lbl, weather_strings[ui.lang].next);
    } else {
        lv_label_set_text_fmt(ui.wifi_pw_prompt, "%s %s",
                              weather_strings[ui.lang].enter_password, ui.wifi_pending_ssid);
        lv_label_set_text(ui.wifi_connect_lbl, weather_strings[ui.lang].connect);
    }
}

/* Manual entry reuses the same pad: first keystroke set is the SSID, then the
 * password. `wifi_manual_mode` says which one the Connect button is committing. */
static void wifi_manual_cb(lv_event_t *e) {
    LV_UNUSED(e);
    ui.wifi_manual_mode = true;
    lv_obj_add_flag(ui.wifi_list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.wifi_pw_view, LV_OBJ_FLAG_HIDDEN);
    ui.wifi_pending_ssid[0] = '\0';
    lv_textarea_set_text(ui.wifi_pw_ta, "");
    lv_textarea_set_password_mode(ui.wifi_pw_ta, false);
    lv_label_set_text(ui.wifi_pw_prompt, weather_strings[ui.lang].enter_ssid);
    lv_label_set_text(ui.wifi_connect_lbl, weather_strings[ui.lang].next);
    lv_label_set_text(ui.wifi_status_lbl, "");
    lv_keyboard_set_textarea(ui.wifi_pw_kb, ui.wifi_pw_ta);
}

static void wifi_scan_cb(lv_event_t *e) {
    LV_UNUSED(e);
    lv_label_set_text(ui.wifi_status_lbl, weather_strings[ui.lang].scanning);
    lv_obj_clean(ui.wifi_list);
    if (ui.on_wifi_scan) ui.on_wifi_scan();
}

static void wifi_pw_show_cb(lv_event_t *e) {
    LV_UNUSED(e);
    bool hidden = lv_textarea_get_password_mode(ui.wifi_pw_ta);
    lv_textarea_set_password_mode(ui.wifi_pw_ta, !hidden);
    lv_label_set_text(ui.wifi_pw_show_lbl,
                      hidden ? weather_strings[ui.lang].hide : weather_strings[ui.lang].show);
}

static void wifi_connect_cb(lv_event_t *e) {
    LV_UNUSED(e);
    const char *txt = lv_textarea_get_text(ui.wifi_pw_ta);
    if (ui.wifi_manual_mode) {
        /* Step one of the manual path: capture the SSID, then ask for the password. */
        if (!txt || !txt[0]) return;
        char ssid[33];
        snprintf(ssid, sizeof ssid, "%s", txt);
        lv_label_set_text(ui.wifi_connect_lbl, weather_strings[ui.lang].connect);
        wifi_show_password(ssid);
        return;
    }
    if (!ui.wifi_pending_ssid[0]) return;
    lv_label_set_text(ui.wifi_status_lbl, weather_strings[ui.lang].connecting);
    if (ui.on_wifi_connect) ui.on_wifi_connect(ui.wifi_pending_ssid, txt ? txt : "");
}

static void wifi_row_click_cb(lv_event_t *e) {
    const char *ssid = (const char *)lv_event_get_user_data(e);
    lv_label_set_text(ui.wifi_connect_lbl, weather_strings[ui.lang].connect);
    wifi_show_password(ssid);
}

/* Frees the ssid string handed to wifi_row_click_cb as user data. */
static void wifi_row_free_cb(lv_event_t *e) { lv_free(lv_obj_get_user_data(lv_event_get_target_obj(e))); }

static lv_obj_t *wifi_text_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **lbl_out) {
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 36);
    lv_obj_set_style_pad_hor(btn, 14, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, C_DIVIDER, 0);
    lv_obj_set_style_radius(btn, R_MD, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, C_TEXT, 0);
    lv_obj_set_style_text_font(l, FONT_14, 0);
    lv_obj_center(l);
    if (lbl_out) *lbl_out = l;
    return btn;
}

static void build_wifi_screen(lv_obj_t *parent) {
    ui.wifi_backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.wifi_backdrop);
    lv_obj_set_size(ui.wifi_backdrop, LV_PCT(100), LV_PCT(100));
    /* FIX: see the matching comment in build_detail_panel() — a 100%-height
     * sibling in root's column flex fights content's flex_grow for space instead
     * of overlaying the full screen. */
    lv_obj_add_flag(ui.wifi_backdrop, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(ui.wifi_backdrop, C_BG, 0);
    lv_obj_set_style_bg_opa(ui.wifi_backdrop, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ui.wifi_backdrop, 20, 0);
    lv_obj_set_flex_flow(ui.wifi_backdrop, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.wifi_backdrop, 10, 0);
    lv_obj_add_flag(ui.wifi_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.wifi_backdrop, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_obj_create(ui.wifi_backdrop);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    ui.wifi_title_lbl = lv_label_create(head);
    lv_label_set_text(ui.wifi_title_lbl, weather_strings[ui.lang].wifi_title);
    lv_obj_set_style_text_color(ui.wifi_title_lbl, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.wifi_title_lbl, FONT_20, 0);
    wifi_text_btn(head, weather_strings[ui.lang].back, wifi_close_cb, &ui.wifi_back_lbl);

    ui.wifi_status_lbl = lv_label_create(ui.wifi_backdrop);
    lv_label_set_text(ui.wifi_status_lbl, "");
    lv_obj_set_style_text_color(ui.wifi_status_lbl, C_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ui.wifi_status_lbl, FONT_14, 0);

    /* -- view 1: scan results -- */
    ui.wifi_list_view = lv_obj_create(ui.wifi_backdrop);
    lv_obj_remove_style_all(ui.wifi_list_view);
    lv_obj_set_size(ui.wifi_list_view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(ui.wifi_list_view, 1);
    lv_obj_set_flex_flow(ui.wifi_list_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.wifi_list_view, 10, 0);
    lv_obj_remove_flag(ui.wifi_list_view, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *actions = lv_obj_create(ui.wifi_list_view);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 10, 0);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    wifi_text_btn(actions, weather_strings[ui.lang].scan, wifi_scan_cb, &ui.wifi_scan_btn_lbl);
    wifi_text_btn(actions, weather_strings[ui.lang].enter_manually, wifi_manual_cb, &ui.wifi_manual_btn_lbl);

    ui.wifi_list = lv_obj_create(ui.wifi_list_view);
    lv_obj_remove_style_all(ui.wifi_list);
    lv_obj_set_size(ui.wifi_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(ui.wifi_list, 1);
    /* FIX: flex_grow claims all free space in wifi_list_view regardless of the
     * forget-network row that now follows it — LVGL doesn't reserve room for a
     * fixed-size sibling placed after a grow item here, so without a cap the list
     * pushes it fully out of the (non-scrollable) backdrop. Capped low enough that
     * even a full APP_WIFI_MAX_SCAN-sized list still leaves the row visible; the
     * list itself already scrolls (LV_DIR_VER below) once it hits this height. */
    lv_obj_set_style_max_height(ui.wifi_list, 340, 0);
    lv_obj_set_flex_flow(ui.wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.wifi_list, 6, 0);
    lv_obj_set_scroll_dir(ui.wifi_list, LV_DIR_VER);

    /* Added on request, now folded into the design (Claude Design project
     * "Weather app with 7-day forecast", synced 2026-09-09): its own full-width
     * btn-secondary row after the network list, not grouped with Scan/Enter
     * manually — see Weather App.dc.html's wifiShowingList block. */
    lv_obj_t *forget_btn = wifi_text_btn(ui.wifi_list_view, weather_strings[ui.lang].forget_network,
                                         wifi_forget_cb, &ui.wifi_forget_btn_lbl);
    lv_obj_set_width(forget_btn, LV_PCT(100));

    /* -- view 2: SSID / password pad -- */
    ui.wifi_pw_view = lv_obj_create(ui.wifi_backdrop);
    lv_obj_remove_style_all(ui.wifi_pw_view);
    lv_obj_set_size(ui.wifi_pw_view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(ui.wifi_pw_view, 1);
    lv_obj_set_flex_flow(ui.wifi_pw_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.wifi_pw_view, 8, 0);
    lv_obj_add_flag(ui.wifi_pw_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.wifi_pw_view, LV_OBJ_FLAG_SCROLLABLE);

    ui.wifi_pw_prompt = lv_label_create(ui.wifi_pw_view);
    lv_label_set_text(ui.wifi_pw_prompt, "");
    lv_obj_set_style_text_color(ui.wifi_pw_prompt, C_TEXT, 0);
    lv_obj_set_style_text_font(ui.wifi_pw_prompt, FONT_16, 0);

    lv_obj_t *pw_row = lv_obj_create(ui.wifi_pw_view);
    lv_obj_remove_style_all(pw_row);
    lv_obj_set_size(pw_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pw_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pw_row, 10, 0);
    lv_obj_remove_flag(pw_row, LV_OBJ_FLAG_SCROLLABLE);
    ui.wifi_pw_ta = lv_textarea_create(pw_row);
    lv_textarea_set_one_line(ui.wifi_pw_ta, true);
    /* FIX: same as ui.search_ta — no explicit font meant ASCII-only fallback. */
    lv_obj_set_style_text_font(ui.wifi_pw_ta, FONT_14, 0);
    lv_obj_set_flex_grow(ui.wifi_pw_ta, 1);
    wifi_text_btn(pw_row, weather_strings[ui.lang].show, wifi_pw_show_cb, &ui.wifi_pw_show_lbl);
    wifi_text_btn(pw_row, weather_strings[ui.lang].connect, wifi_connect_cb, &ui.wifi_connect_lbl);
    wifi_text_btn(pw_row, weather_strings[ui.lang].back, wifi_back_to_list_cb, &ui.wifi_back2_lbl);

    ui.wifi_pw_kb = lv_keyboard_create(ui.wifi_pw_view);
    lv_obj_set_size(ui.wifi_pw_kb, LV_PCT(100), 220);
    lv_keyboard_set_textarea(ui.wifi_pw_kb, ui.wifi_pw_ta);
    kb_add_umlauts(ui.wifi_pw_kb);
    lv_keyboard_set_mode(ui.wifi_pw_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
}

void weather_ui_set_wifi_callbacks(weather_ui_wifi_scan_cb_t on_scan, weather_ui_wifi_connect_cb_t on_connect,
                                    weather_ui_wifi_forget_cb_t on_forget) {
    ui.on_wifi_scan = on_scan;
    ui.on_wifi_connect = on_connect;
    ui.on_wifi_forget = on_forget;
}

void weather_ui_set_brightness_callback(weather_ui_brightness_cb_t on_brightness) {
    ui.on_brightness = on_brightness;
}

void weather_ui_set_brightness_adaptive_callback(weather_ui_brightness_adaptive_cb_t on_adaptive) {
    ui.on_brightness_adaptive = on_adaptive;
}

void weather_ui_open_wifi_setup(void) {
    if (!ui.wifi_backdrop) return;
    wifi_show_list();
    lv_obj_clean(ui.wifi_list);
    lv_label_set_text(ui.wifi_status_lbl, weather_strings[ui.lang].scanning);
    lv_obj_remove_flag(ui.wifi_backdrop, LV_OBJ_FLAG_HIDDEN);
    if (ui.on_wifi_scan) ui.on_wifi_scan();
}

void weather_ui_set_wifi_scan_results(const wx_wifi_network_t *networks, int count) {
    if (!ui.wifi_list) return;
    lv_obj_clean(ui.wifi_list);
    lv_label_set_text(ui.wifi_status_lbl, count == 0 ? weather_strings[ui.lang].no_networks : "");
    for (int i = 0; i < count; i++) {
        lv_obj_t *row = lv_obj_create(ui.wifi_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, C_SURFACE, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, C_DIVIDER, 0);
        lv_obj_set_style_radius(row, R_MD, 0);
        lv_obj_set_style_pad_all(row, 12, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        char *ssid_copy = lv_malloc(33);
        if (ssid_copy) {
            snprintf(ssid_copy, 33, "%s", networks[i].ssid);
            lv_obj_set_user_data(row, ssid_copy);
            lv_obj_add_event_cb(row, wifi_row_free_cb, LV_EVENT_DELETE, NULL);
            lv_obj_add_event_cb(row, wifi_row_click_cb, LV_EVENT_CLICKED, ssid_copy);
        }

        lv_obj_t *texts = lv_obj_create(row);
        lv_obj_remove_style_all(texts);
        lv_obj_set_size(texts, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(texts, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *n = lv_label_create(texts);
        lv_label_set_text(n, networks[i].ssid);
        lv_obj_set_style_text_color(n, C_TEXT, 0);
        lv_obj_set_style_text_font(n, FONT_16, 0);
        lv_obj_t *sub = lv_label_create(texts);
        lv_label_set_text(sub, networks[i].secured ? weather_strings[ui.lang].secured
                                                   : weather_strings[ui.lang].open_net);
        lv_obj_set_style_text_color(sub, C_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(sub, FONT_12, 0);

        /* Signal strength as four stepped bars — a magnitude, so one hue with the
         * unreached steps dimmed rather than four different colours. */
        lv_obj_t *bars = lv_obj_create(row);
        lv_obj_remove_style_all(bars);
        lv_obj_set_size(bars, 26, 16);
        lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_set_style_pad_column(bars, 2, 0);
        lv_obj_remove_flag(bars, LV_OBJ_FLAG_SCROLLABLE);
        for (int b = 0; b < 4; b++) {
            lv_obj_t *bar = lv_obj_create(bars);
            lv_obj_remove_style_all(bar);
            lv_obj_set_size(bar, 4, 4 + b * 4);
            lv_obj_set_style_radius(bar, 1, 0);
            lv_obj_set_style_bg_color(bar, C_ACCENT, 0);
            lv_obj_set_style_bg_opa(bar, b <= networks[i].strength ? LV_OPA_COVER : LV_OPA_20, 0);
        }
    }
}

void weather_ui_set_wifi_connect_result(bool success) {
    if (!ui.wifi_status_lbl) return;
    if (success) {
        lv_label_set_text(ui.wifi_status_lbl, weather_strings[ui.lang].connected);
        lv_obj_add_flag(ui.wifi_backdrop, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(ui.wifi_status_lbl, weather_strings[ui.lang].error);
    }
}

/* ---- hourly + status ------------------------------------------------------ */

void weather_ui_set_hourly(const weather_hourly_t *today, const weather_hourly_t *const days[WEATHER_UI_DAYS]) {
    /* Copy: the caller's buffers are the network task's and may be reused. */
    ui.hourly_today_valid = today != NULL;
    if (today) ui.hourly_today = *today;
    for (int i = 0; i < WEATHER_UI_DAYS; i++) {
        bool ok = days && days[i];
        ui.hourly_day_valid[i] = ok;
        if (ok) ui.hourly_days[i] = *days[i];
    }
    if (ui.today_chart)
        weather_chart_set_data(ui.today_chart, ui.hourly_today_valid ? &ui.hourly_today : NULL,
                               ui.temp_unit == WX_UNIT_F);
}

void weather_ui_set_network_status(wx_net_status_t status) {
    if (!ui.header_net_lbl) return;
    bool online = (status == WX_NET_ONLINE);
    lv_label_set_text(ui.header_net_lbl, online ? weather_strings[ui.lang].online
                                                : weather_strings[ui.lang].offline);
    /* FIX: netDotColor in the design is var(--color-accent) / --color-neutral-500;
     * C_GOOD was a green this design system does not carry. */
    lv_obj_set_style_bg_color(ui.header_net_dot, online ? C_ACCENT : C_TEXT_MUTED, 0);
}

void weather_ui_set_data_stale(bool stale) {
    if (!ui.header_stale_lbl) return;
    if (stale) lv_obj_remove_flag(ui.header_stale_lbl, LV_OBJ_FLAG_HIDDEN);
    else       lv_obj_add_flag(ui.header_stale_lbl, LV_OBJ_FLAG_HIDDEN);
}

/* ---- entry point ---------------------------------------------------------- */

void weather_ui_create(lv_obj_t *parent) {
    memset(&ui, 0, sizeof(ui));
    ui.lang = LANG_EN;
    ui.temp_unit = WX_UNIT_C;
    ui.wind_unit = WX_WIND_KMH;
    ui.time_fmt = WX_TIME_24;
    ui.brightness = 100;

    ui.root = lv_obj_create(parent);
    lv_obj_remove_style_all(ui.root);
    /* The design is authored at exactly the panel's 1024x600; track the parent so a
     * rotated or differently-sized display still fills correctly. */
    lv_obj_set_size(ui.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ui.root, C_BG, 0);
    lv_obj_set_style_bg_opa(ui.root, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(ui.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(ui.root, LV_OBJ_FLAG_SCROLLABLE);

    build_header(ui.root);
    build_error_bar(ui.root);
    build_refresh_toast(ui.root);

    lv_obj_t *content = lv_obj_create(ui.root);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_pad_all(content, 24, 0);
    lv_obj_set_style_pad_top(content, 8, 0);
    lv_obj_set_style_pad_bottom(content, 20, 0);   /* FIX: the design's padding is 8px 24px 20px */
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 14, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    build_current_card(content);

    /* Today's hourly series. The design's HTML drew this inline on the current
     * card; at 1024x600 that card is already a full row, so it gets its own band
     * directly beneath — same reading order, no crowding. */
    ui.today_chart = weather_chart_create(content, LV_PCT(100), 108);

    build_forecast_strip(content);

    build_settings_panel(ui.root);
    build_device_info_panel(ui.root);
    build_search_overlay(ui.root);
    build_detail_panel(ui.root);
    build_wifi_screen(ui.root);
}
