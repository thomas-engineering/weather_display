# Handoff: LVGL Weather App UI

## Overview
C/LVGL v9 port of the "Weather App" HTML design (`Weather App.dc.html`, Nocturne design system) for a 1024x600 landscape touch panel. This bundle is the **actual C source**, not an HTML reference — it's ready to compile against LVGL v9, not a design-only handoff.

## Status
Actively being corrected against LVGL-rendered screenshots from the real device. Fixed so far:
- Root sized to the real panel (1024x600).
- Hourly chart: precipitation now rendered as a real bar-chart series overlaid on the temperature line chart (previously faked with an over-wide line, which rendered as disconnected purple blobs).
- Humidity stat box now shows the category text **and** the percentage sub-line, matching the HTML design.
- Dialog backdrops (settings / Wi-Fi / day-detail) recolored from pure black to `C_NEUTRAL_900` at ~50% opacity, matching the design system's `.dialog-backdrop` (`color-mix(neutral-900, transparent)`).
- Settings dialog: added the missing close (X) button next to the title, and moved the "Network" field to the end of the field list (was first).

## Known remaining risks / things to verify on-device
- Fonts: currently built-in `lv_font_montserrat_*`, not the Nocturne `Inter` typeface — convert an Inter TTF with `lv_font_conv` for a pixel-accurate match (see README_LVGL.md).
- Backdrop translucency: if the panel/driver doesn't alpha-blend, translucent overlays (settings/Wi-Fi/day-detail) may still render as fully opaque — check display driver / `LV_COLOR_DEPTH` config if so.
- Settings panel labels don't re-localize on language change (documented TODO in `weather_ui.c` above `on_seg_lang`).
- Networking, date/weekday formatting, and the "real feel" sentence are intentionally left to the embedding app (see README_LVGL.md "Integration").

## Design reference
The source of truth for visual intent is `Weather App.dc.html` (HTML/CSS, Nocturne design system) in the parent project — colors, spacing and copy in `weather_ui.c`'s `#define`s at the top are hand-copied from that file's inline styles / `styles.css` tokens. If a discrepancy is found, treat the HTML file as correct and this C file as the thing to fix.

## Files
- `weather_ui.h` / `weather_ui.c` — the screen, layout, styling and interaction logic.
- `weather_icons.h` / `weather_icons.c` — weather glyphs drawn from LVGL primitives.
- `weather_i18n.h` / `weather_i18n.c` — DE/EN/ES/FR string tables.
- `README_LVGL.md` — original integration guide (networking hooks, font swap instructions, LVGL v8 note, etc).

## Next steps for Claude Code
1. Wire `weather_ui_set_callbacks` / `weather_ui_set_current` / `weather_ui_set_days` to a real HTTP client + JSON parser (Open-Meteo API, see README_LVGL.md).
2. Convert Inter to an LVGL bitmap font and swap the `&lv_font_montserrat_NN` references.
3. Verify backdrop alpha-blending on actual hardware; if unsupported, consider an animated fade-in solid tint instead.
4. Re-check against any further screenshots for remaining visual deviations.
