# Review: weather icons (lvgl-reviewer, 2026-11-02)

Reviewed commit b216372 on `feat/weather-icon-refresh`. Condensed from the
reviewer's report; "Status" records what the follow-up commit did. Frame costs
were derived from the LVGL 9.5 source and not measured on the board.

## [HOCH] Rotated single objects create transform layers in the render path
Rays (partly) and flake arms (snow) were `lv_obj` rects with `transform_rotation`:
up to 63 transform layers on the main screen, each allocating on every redraw.
Status: fixed. Sun with rays and the three flakes are one A8 mask each,
rasterized per pixel size (nonzero winding) and drawn 1:1. The clear icon uses
the same path. Objects per icon: partly 14 -> 5, snow 14 -> 5, clear 10 -> 1.
Verify: `./scripts/sim.sh --screen icons`, `./scripts/fw-build.sh`; the board
check (detail sheet over a partly or snow day, no new `lvgl_stall` in
`.logs/hw.log`) is still open.

## [HOCH] Flakes and partly rays unreadable on 36 px tiles
Integer truncation gave 1 px strokes. Status: fixed by rasterizing at the target
size with exact coverage; flakes read as stars and the bolt as a bolt on the
36 px tiles in `sim.sh --screen main`.

## [MITTEL] Geometry deviated from WeatherIcon.dc.html
Status: resolved the other way round. Claude Design now carries the revised
icons (`ICONS.md`); the firmware follows it: partly sun r 9 with 2.8 x 6 rays,
design bolt outline (scaled to fit under the firmware's taller cloud), snow
flakes 14 x 3.2. The rain drop shift is documented in the code.

## [MITTEL] FOG_CLOUD_COLOR
The token value (neutral-600) is correct and now matches the design's fixed
`#75798C` fog cloud; the header documents that fog ignores `color`. The same hex
in `weather_saver.c` is that file's own palette entry and was left alone.

## [MITTEL] Icon gallery missed device sizes and `--shots`
Status: fixed. `--screen icons` shows every icon at 72, 64 and 36 px, the count
is tied to the enum with a static assert, and `icons` is part of `--shots`.

## [NIEDRIG] Longer icon rebuild under the display lock
Mostly resolved by the object-count reduction. Not done: skipping the rebuild
when type, size and color are unchanged (a predicate for `app_logic`).

## [NIEDRIG] New literals
Status: fixed. Ray angles derive from `SUN_RAY_COUNT`; the tenths-of-degree
conversion is only used for the arrow.

## Found while comparing with the design
`weather_ui.c` built the current-weather icon at 64 px but rebuilt it at 72 px,
and the day-detail icon at 64 px; the design says 64 and 72. Fixed with
`CURRENT_ICON_PX` and `DETAIL_ICON_PX`.
