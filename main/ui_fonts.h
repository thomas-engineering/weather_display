/* Font mapping for the weather UI.
 *
 * Two options, chosen at build time:
 *
 *  - Inter (CONFIG_WEATHER_USE_INTER_FONTS, the default): the typeface the Nocturne
 *    design system actually specifies, generated into main/fonts/ by tools/gen_fonts.sh
 *    with the full Latin-1 supplement — required for the German, Spanish and French
 *    string tables. Caption sizes are Inter Regular (--font-body, 400), display sizes
 *    are Inter Medium (--font-heading-weight, 500).
 *
 *  - LVGL's built-in Montserrat: no extra flash, but ASCII only, so accented
 *    characters render as blanks. English-only fallback.
 *
 * LV_SYMBOL_* glyphs (the GPS pin, refresh and settings icons) are FontAwesome
 * codepoints baked into the Montserrat builds only — they are NOT in the generated
 * Inter fonts, so anything drawing a symbol must use FONT_SYMBOL.
 */
#ifndef UI_FONTS_H
#define UI_FONTS_H

#include "lvgl.h"
#include "sdkconfig.h"

#if defined(CONFIG_WEATHER_USE_INTER_FONTS)

LV_FONT_DECLARE(inter_10)
LV_FONT_DECLARE(inter_12)
LV_FONT_DECLARE(inter_14)
LV_FONT_DECLARE(inter_16)
LV_FONT_DECLARE(inter_18)
LV_FONT_DECLARE(inter_20)
LV_FONT_DECLARE(inter_22)
LV_FONT_DECLARE(inter_30)
LV_FONT_DECLARE(inter_36)
LV_FONT_DECLARE(inter_48)

#define FONT_10 (&inter_10)
#define FONT_12 (&inter_12)
#define FONT_14 (&inter_14)
#define FONT_16 (&inter_16)
#define FONT_18 (&inter_18)
#define FONT_20 (&inter_20)
#define FONT_22 (&inter_22)
#define FONT_30 (&inter_30)
#define FONT_36 (&inter_36)
#define FONT_48 (&inter_48)

#else /* Montserrat fallback — ASCII only. */

#define FONT_10 (&lv_font_montserrat_10)
#define FONT_12 (&lv_font_montserrat_12)
#define FONT_14 (&lv_font_montserrat_14)
#define FONT_16 (&lv_font_montserrat_16)
#define FONT_18 (&lv_font_montserrat_18)
#define FONT_20 (&lv_font_montserrat_20)
#define FONT_22 (&lv_font_montserrat_22)
#define FONT_30 (&lv_font_montserrat_30)
#define FONT_36 (&lv_font_montserrat_36)
#define FONT_48 (&lv_font_montserrat_48)

#endif

/* Always a Montserrat build: this is the only place the LV_SYMBOL_* glyphs exist. */
#define FONT_SYMBOL (&lv_font_montserrat_18)
/* Synced from Claude Design 2026-09-10: the header's refresh/settings icon
 * buttons grew from 20px to 26px glyphs alongside their 44->56px touch
 * targets. */
#define FONT_SYMBOL_LG (&lv_font_montserrat_26)

#endif
