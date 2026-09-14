#ifndef FAVORITES_H
#define FAVORITES_H

#include <stdbool.h>

/* Same slot count as APP_WEATHER_MAX_RESULTS (main/app_weather.h) and the
 * Claude Design source's `favorites: new Array(6).fill(null)` — not derived
 * from it to keep this component hardware-free (see CLAUDE.md's
 * Codeorganisation), just kept numerically in sync by convention. */
#define APP_FAVORITES_MAX 6

typedef struct __attribute__((packed)) {
    char name[64];
    char country[64];
    float lat, lon;
    bool used;
} app_favorite_t;

/* Toggles a city in favs: if a used slot already matches by lat/lon, that
 * slot is cleared (removed); otherwise the city is written into the first
 * unused slot. Matches the design's toggleFavorite(): if every slot is
 * already used and none matches, this is a no-op (returns false) rather
 * than evicting an existing favorite. Returns true if the slot set changed. */
bool app_favorites_toggle(app_favorite_t favs[APP_FAVORITES_MAX], const char *name,
                           const char *country, float lat, float lon);

/* Clears slot idx. No-op if idx is out of range. */
void app_favorites_remove(app_favorite_t favs[APP_FAVORITES_MAX], int idx);

/* True if some used slot matches lat/lon exactly — the same equality the
 * design's JS state uses (favorites are only ever populated from geocoding-
 * API results, so re-fetching the same city yields the same float value). */
bool app_favorites_contains(const app_favorite_t favs[APP_FAVORITES_MAX], float lat, float lon);

#endif
