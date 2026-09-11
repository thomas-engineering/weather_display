# Design reference

Local copy of the design sources the LVGL UI is ported from. Nothing here is
compiled — it is the reference the firmware is checked against.

Pulled from Claude Design on 2026-09-08 with the `DesignSync` tool.
`src/weather-app/Weather App.dc.html` re-pulled 2026-09-09 for the
`forgetSavedNetwork` addition (`t.forgetSavedNetwork` button in the
`wifiShowingList` block, own full-width `btn-secondary` row after the network
list) — ported to `main/weather_ui.c`'s `build_wifi_screen` the same day.
Re-pulled again 2026-09-10 for touch-target enlargements (most interactive
controls now target >=44px for the 7" panel: header icon buttons, the location
button, dialog close buttons, segmented control options, the brightness
slider's hit area, and the retry/cancel/configure-network buttons) — ported to
the corresponding `weather_ui.c` builders the same day. That pass also
surfaced two LVGL-side bugs unrelated to the design itself, both fixed in
`weather_ui.c`: the current-weather stat boxes (Humidity/Feels like/Wind/
Precip.) weren't equal height because LVGL's flex row has no stretch
cross-align to match the design's CSS grid, and every full-screen dialog
backdrop (`settings_backdrop`, `detail_backdrop`, `wifi_backdrop`,
`search_backdrop`) was a normal flex child of `ui.root`'s column layout instead
of being excluded from it, so once shown it landed pushed down and clipped
instead of overlaying the whole screen — that's also why the Settings dialog
never actually centered despite `lv_obj_center()`.
Re-pulled again 2026-09-10 for a main-screen readability pass: the header
clock/date grew (20/12px -> 30/18px, needing a new `inter_30.c` — see
`tools/gen_fonts.sh`, regenerated from the v4.1 Inter release with the same
Latin-1 range and Montserrat-18 fallback as the other eight sizes), the
current-weather card's icon/temp gap and column tightened (20px -> 16px, temp
column 170px -> 150px), and the Humidity/Feels like/Wind/Precip. grid went
from four equal 104px columns to `128px 96px 96px 96px` with larger type
(11/16/13px) — Humidity carries a third line the other three don't, so it
gets the extra width instead of the row growing taller than its siblings, the
same 44px-wide question the touch-target pass raised for a different reason.
The day-detail sheet's own (visually identical but smaller) stat boxes were
not part of this change and were left alone — `stat_box_create()` now takes
`width`/`pad_hor`/`big` so the two call sites can diverge on purpose.
Re-pulled again 2026-09-10 for two changes: the header's refresh/settings icon
buttons grew again (44px -> 56px, glyph 20px -> 26px, needing
`CONFIG_LV_FONT_MONTSERRAT_26` — no generated Inter equivalent needed since
`LV_SYMBOL_*` glyphs only exist in Montserrat), with an extra `--space-6` gap
on both sides of the clock/date group to keep them from crowding the bigger
buttons; and a new refresh toast — tapping the header refresh icon or the
error bar's retry button (both share `refresh()`/`on_refresh`) now shows
"Weather data updated." for 1s on success, or the existing error message,
staying until tapped away, on failure. Ported to `weather_ui.c`
(`icon_btn_create`, new `build_refresh_toast`/`weather_ui_show_refresh_toast`)
and `app_weather.c` (`do_refresh()` gained an `is_manual` flag so only the
refresh-button path — not periodic auto-refresh, city selection, or the
post-Wi-Fi-connect refresh — triggers the toast, matching the design's own
`refresh()` handler being the only place it sets `refreshToast`). New
`data_updated` string added to `weather_i18n.c` in all four locales, copied
verbatim from the design's `strings` blocks.
Re-pulled again 2026-09-10 for a new "Device information" dialog: an info
button (filled circle, "i") next to the Settings dialog's close button opens
it on top of Settings without closing that dialog first. Shows device name,
hardware version and firmware version always; IP address, DNS, gateway and a
short note only while online, or "Not connected to the internet" while
offline. The Settings dialog's own close button also grew 44px -> 56px in the
same pull, matching the info button next to it. New strings (`deviceInfo`,
`deviceName`, `hardwareVersion`, `firmwareVersion`, `ipAddress`, `dns`,
`gateway`, `notConnected`) added to `weather_i18n.c`/`.h` in all four locales.
Ported to `weather_ui.c` (`build_device_info_panel`, `weather_ui_set_device_info`,
`device_info_rebuild` for language-change re-localization — same pattern as
`settings_rebuild`) and `app_wifi.c` (new `app_wifi_get_ip_info()`) /
`app_weather.c` (`push_device_info_to_ui()`, firmware version from
`esp_app_get_description()`, hardware version hardcoded to this board's fixed
silicon revision per CLAUDE.md). The design has no built-in glyph for the info
icon (LVGL's symbol font has none either), so it's composed from a small
circle + "i" label instead of a font glyph — same approach as
`weather_icons.c`'s hand-drawn condition icons.

**Confirmed on hardware** (2026-09-10): info button responds to taps, dialog
opens on top of Settings, and the stacked-backdrop transparency (two
`LV_OPA_50` overlays: Device information's own backdrop over the dimmed
Settings dialog beneath it) renders correctly — Settings shows through faintly
as intended, not as a solid opaque panel.

Also caught a bug in the port itself while wiring the new button: LVGL's
`lv_obj_create()` defaults to `LV_OBJ_FLAG_CLICKABLE`, and the info button's
purely decorative inner circle inherited it, intercepting every tap before it
reached the button's own handler. Found via `sim/main.c`'s click-by-label-text
helper (which walks up to the first clickable ancestor, the same way real
touch input targets the deepest clickable object under a point) rather than
on hardware — added a `device-info` screen state to `sim/main.c` and
`scripts/sim.sh --shots` alongside the fix.

## `src/weather-app/` — the app design

The "Weather app with 7-day forecast" Claude Design project (ID withheld —
it's a private project, not needed to work with this copy). This is the
**source of truth for visual intent**: when the LVGL
rendering and this disagree, this is right and the C is what needs fixing.

```
src/weather-app/
├── Weather App.dc.html                the screen design — HTML/CSS, 1024x600
├── WeatherIcon.dc.html                the seven weather glyphs as SVG
├── support.js                         the canvas runtime the .dc.html files need
└── _ds/nocturne-<uuid>/               the design system as the design links it
```

These `.dc.html` files run inside Claude Design's canvas, not standalone: they
need `window.React` and the design-system bundle, so opening one in a browser
will not render it. Read them as source.

The design project also has a `design_handoff_lvgl_weather_ui/` bundle — its
own C export attempt of the same screens. It was copied here once (as of
2026-09-08 14:43) but removed on 2026-09-12: it never moved again after that
copy while `main/`'s actual LVGL code kept evolving well past it (Wi-Fi
provisioning, device-info dialog, last-update field, current LVGL API), it
wasn't referenced by any build or script, and its own translation of the
design was independently found unfaithful to the source SVGs in at least one
case (the sun icon's rays). Treat `main/` as the current implementation and
the `.dc.html`/`.svg` files above as the only source of visual truth; don't
re-copy `design_handoff_lvgl_weather_ui/` without checking it has actually
moved since.

## `src/nocturne/` — the design system

The "Nocturne" Claude Design project (ID withheld, same reason as above).
Where every colour, spacing,
radius and shadow in `main/weather_ui.c`'s `#define` block comes from — those
values were hand-copied, so `styles.css` is the file to check them against.

```
src/nocturne/
├── styles.css          the token sheet + component layer — the source of truth
├── theme.json          the parameters the system was generated from
├── readme.md           the written rules, incl. the LVGL/ESP32-P4 constraints
├── foundations/        color, type, layout, icons, image
└── components/         buttons, cards, dialog, forms, navigation, table
```

Spot checks against the port, all matching: `--color-neutral-500 #9397ab` =
`C_TEXT_MUTED`, `--color-neutral-800 #3f424d` = `C_NEUTRAL_800`,
`--color-accent-600 #796cbf` = `C_ACCENT_600`, `--radius-md 8px` = `R_MD`.

### Not copied

- `assets/photo.jpg` — a reference photograph; `foundations/image.html` links it
  and shows a broken image locally. There are no photographs on the device.
- `templates/deck/`, `templates/landing/` — slide-deck and landing-page starters.
- `theme.html`, `thumbnail.html`, `_ds_*` — the project's cover and pane
  machinery.
- `uploads/` in the app project — simulator screenshots handed over for review;
  the same set is produced here by `./scripts/sim.sh --shots`.

## Keeping it current

This is a copy, not a live link. Nothing warns you when the design moves —
re-pull after any design change. The C export arrives separately as a zip and is
merged into `main/` by hand; see "Imported vs. authored" in the top-level README.
