#ifndef APP_FAVORITES_H
#define APP_FAVORITES_H

#include "favorites.h"

/* Loads persisted favorites from NVS into RAM (or leaves them all empty if
 * no valid record exists yet). Call once at boot, before anything reads
 * app_favorites_get(). */
void app_favorites_load(void);

/* Returns the live APP_FAVORITES_MAX-entry array; never NULL. */
const app_favorite_t *app_favorites_get(void);

/* Toggles a city and debounce-persists the change if the slot set actually
 * changed (see app_favorites_toggle() in favorites.h for the matching/
 * no-op-when-full rules). Returns whether it changed, so the caller knows
 * whether to re-render. */
bool app_favorites_toggle_save(const char *name, const char *country, float lat, float lon);

/* Clears slot idx and debounce-persists the change. */
void app_favorites_remove_save(int idx);

#endif
