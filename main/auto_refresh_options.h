#pragma once

/* Auto-refresh choices in Settings, in display order (Off/15/30/60 min).
 * No ESP-IDF headers: weather_ui.c includes this and sim/ builds it natively.
 *
 * app_prefs.c persists the *index* into this table in NVS (auto_refresh_idx),
 * so the order is part of the storage format: only ever append. */

#define AUTO_REFRESH_OPTION_COUNT 4
#define AUTO_REFRESH_DEFAULT_IDX 2

static const int k_auto_refresh_values[AUTO_REFRESH_OPTION_COUNT] = { 0, 15, 30, 60 };
