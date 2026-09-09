# Weather App — LVGL port

A C/LVGL (v9.x API) port of the `Weather App.dc.html` design: header with location,
live clock and settings, a current-conditions card, a 7-day forecast strip, a
settings panel (language + units), a full-screen city search with the built-in
on-screen keyboard, and a slide-up/slide-down day-detail panel.

## Files

- `weather_ui.h` / `weather_ui.c` — the whole screen and its interaction logic.
- `weather_icons.h` / `weather_icons.c` — solid weather glyphs built from plain
  LVGL primitives (circles, rounded rects, one polyline for the lightning bolt) —
  no image assets needed, mirrors the hand-drawn SVG icons in the HTML version.
- `weather_i18n.h` / `weather_i18n.c` — the same DE/EN/ES/FR string tables as the
  HTML app's `getStrings()`.

## What this port does NOT do (by design — see "Integration" below)

LVGL is a GUI toolkit, not a networking or i18n-date-formatting library, so three
things are deliberately left to the app that embeds this UI:

1. **Networking.** Nothing here calls Open-Meteo. `weather_ui_set_current()` /
   `weather_ui_set_days()` take already-fetched data; the `on_search` /
   `on_select_city` / `on_refresh` callbacks tell you *when* to fetch. Wire them to
   your platform's HTTP client (esp_http_client on ESP-IDF, libcurl on
   Linux/desktop targets, etc.) and a small JSON parser (cJSON is the common
   pairing with LVGL projects).
2. **Weekday/date formatting and the "real feel" sentence.** These need locale
   data broader than LVGL ships. Format `day_label`, `date_label`, `time_str`,
   `date_str` and `real_feel_text` in the app (a tiny helper mirroring the logic
   in the original `Weather App.dc.html`'s `fmtDate`/`fmtTime`/`realFeelText` is
   enough) and pass the finished strings in.
3. **Settings-panel label re-localization.** Switching language re-renders all
   weather data immediately; the settings panel's own captions only build once.
   See the TODO comment above `on_seg_lang()` in `weather_ui.c` if you want that
   panel to rebuild on language change too.

## Integration sketch

```c
#include "weather_ui.h"

static void on_search(const char *query) { my_app_geocode_async(query); }
static void on_select_city(int idx) { my_app_select_city(idx); /* then re-fetch + weather_ui_set_current/days */ }
static void on_refresh(void) { my_app_fetch_weather_async(); }
static void on_settings_changed(weather_lang_t lang, wx_temp_unit_t t, wx_wind_unit_t w, wx_time_fmt_t tf) {
    my_app_save_prefs(lang, t, w, tf); /* units are display-only inside weather_ui.c — no re-fetch needed */
}

void app_main_ui_init(void) {
    weather_ui_create(lv_scr_act());
    weather_ui_set_callbacks(on_search, on_select_city, on_refresh, on_settings_changed);
    my_app_fetch_weather_async(); /* -> eventually calls weather_ui_set_current/days */
}
```

When your fetch resolves:

```c
weather_current_t cur = {
    .location_name = "Trier", .location_country = "Germany",
    .time_str = "14:05", .date_str = "Thu, Sep 3",
    .weather_code = 3, .temp_c = 21.4f, .feels_like_c = 20.1f,
    .humidity_pct = 64, .wind_kmh = 12.0f, .precip_pct = 10,
    .real_feel_text = "Feels about the same as the air temperature.",
};
weather_ui_set_current(&cur);

weather_day_t days[WEATHER_UI_DAYS] = { /* .day_label="Today", .date_label="Sep 3", ... */ };
weather_ui_set_days(days);
```

## Colors, type, spacing

Nocturne's tokens are hardcoded as `#define`s at the top of `weather_ui.c`
(`C_BG`, `C_SURFACE`, `C_ACCENT`, etc. — same hex values as `styles.css`). LVGL has
no CSS variables, so if you retune the design system, update these `#define`s to
match.

Type uses the built-in Montserrat bitmap fonts (`lv_font_montserrat_*`) at sizes
mirroring the HTML version's px sizes. For a pixel-accurate match to Nocturne's
Inter, convert an Inter TTF with `lv_font_conv` (see the LVGL docs' "Add a new
font" page) at the same weights (400/500) and swap the `&lv_font_montserrat_NN`
references.

Spacing/radius: `R_MD` (8px) / `R_LG` (14px) match `--radius-md` / `--radius-lg`;
padding values are hand-copied from the HTML version's inline styles (they don't
carry the `--space-*` token scale 1:1 since LVGL layouts are integer px).

## On-screen keyboard

The city-search overlay uses LVGL's built-in `lv_keyboard` widget bound to the
search `lv_textarea` — this is exactly the "touch display, no physical keyboard"
requirement from the original brief; no custom keyboard needed.

## Display assumption

1024x600, landscape. `weather_ui_create()` sizes its root object to exactly that;
if your panel is a different resolution, either resize the root after creation or
adjust the hardcoded sizes in `weather_ui.c` (this port doesn't use `LV_PCT` for
the outer root's own size — its *children* do use `LV_PCT`/flex-grow so they
already adapt somewhat to a taller/shorter panel).

## LVGL version note

Written against the LVGL v9 API (`lv_obj_remove_style_all`, `lv_obj_set_flex_*`,
`lv_anim_set_path_cb` etc.). If you're on LVGL v8.x, the flex API and a few
function names differ — most notably v8 has no `lv_obj_remove_style_all` used this
liberally in the same way and uses `lv_obj_set_style_*` selectors slightly
differently. Ask and this port can be adapted to v8.
