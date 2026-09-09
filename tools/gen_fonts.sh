#!/usr/bin/env bash
# Generates LVGL C fonts from Inter — the typeface the Nocturne design system uses.
#
# Why this exists: LVGL's built-in lv_font_montserrat_* cover ASCII plus a handful
# of symbols and nothing else, so every accented character in the German, Spanish
# and French string tables (ü, ä, é, í, ñ, à, û ...) and the em dash in the
# "real feel" sentences would render as a blank box. These builds carry the full
# Latin-1 supplement.
#
# Weights follow the design system: --font-body is Inter 400, --font-heading is
# Inter 500, so the caption sizes are Regular and the display sizes are Medium.
#
# Usage:  tools/gen_fonts.sh [path/to/inter/ttf/dir]
# Output: main/fonts/inter_<size>.c
#
# Requires node (for npx lv_font_conv). Re-run only when you change sizes or ranges;
# the generated .c files are meant to be committed.

set -euo pipefail

TTF_DIR="${1:-}"
OUT="$(cd "$(dirname "$0")/.." && pwd)/main/fonts"
# ASCII + Latin-1 supplement + en/em dash + curly apostrophe.
RANGE="0x20-0x7F,0xA0-0xFF,0x2013-0x2014,0x2019"
BPP=4

REGULAR_SIZES="10 12 14"
MEDIUM_SIZES="16 18 20 22 30 36 48"

if [ -z "$TTF_DIR" ]; then
    echo "usage: $0 <dir containing Inter-Regular.ttf and Inter-Medium.ttf>" >&2
    echo "download: https://github.com/rsms/inter/releases (SIL Open Font License 1.1)" >&2
    exit 1
fi

mkdir -p "$OUT"

gen() {
    local ttf="$1" size="$2"
    echo "  inter_${size}.c  <- $(basename "$ttf") @ ${size}px"
    npx --yes lv_font_conv@1.5.3 \
        --font "$ttf" \
        --size "$size" \
        --bpp "$BPP" \
        --format lvgl \
        --lv-include lvgl.h \
        --range "$RANGE" \
        --no-compress \
        --force-fast-kern-format \
        -o "$OUT/inter_${size}.c"
    # lv_font_conv names the symbol after the output file; make sure it matches.
    sed -i "s/\binter_${size}\b/inter_${size}/g" "$OUT/inter_${size}.c"
    # The generated range is Latin-1, not the FontAwesome codepoints LV_SYMBOL_*
    # uses (LVGL's built-in Montserrat builds only) — anything drawing a symbol
    # in an Inter font (e.g. the on-screen keyboard, which needs both umlauts
    # and its backspace/enter/arrow glyphs) would render that glyph as a tofu
    # box otherwise. Fall back to Montserrat 18 specifically (not the same
    # size) because it's the one size guaranteed compiled into every build —
    # it's what FONT_SYMBOL already resolves to (see ui_fonts.h) on both the
    # simulator (sim/lv_conf.h enables only Montserrat 14/18) and real
    # hardware (sdkconfig enables all nine, but don't assume that elsewhere).
    sed -i "s/\.fallback = NULL,/.fallback = \&lv_font_montserrat_18,/" "$OUT/inter_${size}.c"
}

echo "Generating Inter fonts into $OUT"
for s in $REGULAR_SIZES; do gen "$TTF_DIR/Inter-Regular.ttf" "$s"; done
for s in $MEDIUM_SIZES;  do gen "$TTF_DIR/Inter-Medium.ttf"  "$s"; done
echo "Done. Enable them with CONFIG_WEATHER_USE_INTER_FONTS=y (idf.py menuconfig)."
